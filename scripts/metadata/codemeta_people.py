"""Shared extraction of people (authors, contributors) from CodeMeta v3 data.

This module is the single place where ``codemeta.json`` is interpreted as far as
people are concerned. It is used by ``generate_files.py`` for AUTHORS.md,
CONTRIBUTORS.md and CITATION.cff, so that all generated files agree.

What it understands
-------------------
* Persons identified by ``@id``/``id``, ``identifier``, ``orcid`` or ``sameAs``;
  JSON-LD keywords may be written with or without ``@`` (``type`` / ``@type``).
* ORCID iDs written as ``https://orcid.org/<id>`` or as a bare ``<id>``.
* Roles given directly on a person (``roleName``) or as separate ``Role``
  objects, placed in the ``author`` / ``contributor`` arrays, that point to a
  person with ``schema:author``, ``author``, ``schema:contributor`` or
  ``contributor``. The pointer may be an ORCID (URL or bare), an e-mail address
  or any other identifier used as the person's ``@id``.

``contributorType`` is deliberately *not* interpreted as a role: it belongs to
another CodeMeta/DataCite vocabulary and is not the same thing as ``roleName``.

Nothing is invented: missing information stays missing, and anything that is
skipped or looks inconsistent is reported in :attr:`People.issues`.
"""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass, field
from typing import Any, Iterable, Iterator
from urllib.parse import quote

__all__ = [
    "CodeMetaError",
    "People",
    "Person",
    "as_list",
    "display_identifiers",
    "identity_key",
    "load_people",
    "md_escape",
    "md_link",
    "normalize_orcid",
    "orcid_checksum_ok",
    "sort_people",
]

ROLE_TARGET_KEYS = ("schema:author", "author", "schema:contributor", "contributor")
ROLE_NAME_KEYS = ("roleName", "schema:roleName")
# Properties that may carry an identity (used to match persons and role targets).
IDENTITY_FIELDS = ("@id", "id", "identifier", "orcid", "sameAs", "email")
# Properties searched, in this order, for an ORCID iD.
ORCID_FIELDS = ("@id", "id", "orcid", "identifier", "sameAs")
# Properties whose absolute http(s) URLs are shown as public identifiers.
DISPLAY_FIELDS = ("@id", "id", "identifier")

_ORCID_RE = re.compile(
    r"(?:https?://(?:www\.)?orcid\.org/)?(\d{4}-\d{4}-\d{4}-\d{3}[\dXx])/?",
    re.IGNORECASE,
)
_EMAIL_RE = re.compile(r"[^@\s]+@[^@\s]+\.[^@\s]+")
_HTTP_URL_RE = re.compile(r"https?://\S+", re.IGNORECASE)
_HTTP_PREFIX_RE = re.compile(r"https?://\S", re.IGNORECASE)
_MD_SPECIAL_RE = re.compile(r"([\\`*_\[\]<>|~])")
_MD_ENTITY_RE = re.compile(r"&(?=#?\w+;)")


class CodeMetaError(ValueError):
    """Raised when the CodeMeta document cannot be used."""


@dataclass
class Person:
    """A person as described in CodeMeta, with roles already resolved."""

    given: str = ""
    family: str = ""
    name: str = ""
    orcid: str | None = None  # bare iD, e.g. "0000-0002-1825-0097"
    urls: list[str] = field(default_factory=list)  # public identifiers, no ORCID
    keys: frozenset[str] = frozenset()  # identity keys, used for matching only
    # Identifiers as written: the person's own @id and the value of the
    # ``contributor``/``author`` key of every Role that designates the person.
    other_ids: list[str] = field(default_factory=list)
    affiliations: list[str] = field(default_factory=list)
    roles: list[str] = field(default_factory=list)
    source: str = ""  # location in the document, e.g. "contributor[2]"

    @property
    def orcid_url(self) -> str | None:
        """Canonical ORCID URL, or None."""
        return f"https://orcid.org/{self.orcid}" if self.orcid else None

    @property
    def label(self) -> str:
        """Short text identifying the person in messages."""
        return self.name or self.orcid or self.source or "unnamed person"


@dataclass
class People:
    """Authors and (additional) contributors extracted from one document."""

    authors: list[Person]
    contributors: list[Person]
    issues: list[str]


@dataclass
class _RoleLink:
    """A role pointing at a person through one or more identity keys."""

    keys: frozenset[str]
    role: str
    source: str
    refs: tuple[str, ...] = ()  # the reference to the person, as written


# --------------------------------------------------------------------------- #
# Generic helpers
# --------------------------------------------------------------------------- #


def as_list(value: Any) -> list[Any]:
    """Return ``value`` as a list: None -> [], list -> itself, scalar -> [scalar]."""
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def _unique(items: Iterable[str]) -> list[str]:
    """Remove duplicates while keeping the first occurrence order."""
    return list(dict.fromkeys(items))


def _text(value: Any) -> str:
    """Return a whitespace-normalised string, or '' when ``value`` is not text."""
    return " ".join(value.split()) if isinstance(value, str) else ""


def _iter_strings(value: Any) -> Iterator[str]:
    """Yield the text values found in a string, list or small JSON-LD object."""
    if isinstance(value, str):
        if value.strip():
            yield value.strip()
    elif isinstance(value, list):
        for item in value:
            yield from _iter_strings(item)
    elif isinstance(value, dict):
        for key in ("@id", "id", "@value", "value", "url"):
            yield from _iter_strings(value.get(key))


def _fold(text: str) -> str:
    """Case- and accent-insensitive form of a name, for comparisons."""
    decomposed = unicodedata.normalize("NFKD", text)
    stripped = "".join(c for c in decomposed if not unicodedata.combining(c))
    return " ".join(stripped.casefold().split())


def type_names(item: dict[str, Any]) -> list[str]:
    """Return the JSON-LD types of ``item``, without any prefix or namespace.

    Accepts ``@type`` as well as ``type``, a single value or a list, and values
    such as ``schema:Person`` or ``http://schema.org/Person``.
    """
    names = []
    for value in as_list(item.get("@type", item.get("type"))):
        if isinstance(value, str) and value.strip():
            names.append(
                value.strip().rstrip("/").rsplit("/", 1)[-1].rsplit(":", 1)[-1]
            )
    return names


# --------------------------------------------------------------------------- #
# Identifiers
# --------------------------------------------------------------------------- #


def normalize_orcid(value: Any) -> str | None:
    """Return the bare ORCID iD (``0000-0002-1825-0097``) or None.

    ``https://orcid.org/<id>``, ``http://``, ``www.`` and trailing slashes are
    accepted, and a lower-case final ``x`` is upper-cased.
    """
    if not isinstance(value, str):
        return None
    match = _ORCID_RE.fullmatch(value.strip())
    return match.group(1).upper() if match else None


def orcid_checksum_ok(orcid: str) -> bool:
    """Check the ISO 7064 11,2 check digit of a bare ORCID iD."""
    digits = orcid.replace("-", "")
    total = 0
    for char in digits[:-1]:
        total = (total + int(char)) * 2
    check = (12 - total % 11) % 11
    return digits[-1] == ("X" if check == 10 else str(check))


def identity_key(value: str) -> str | None:
    """Return a canonical key to compare identifiers, whatever their notation.

    ``https://orcid.org/0000-0002-1825-0097`` and ``0000-0002-1825-0097`` give
    the same key; e-mail addresses are compared case-insensitively, with or
    without ``mailto:``; any other identifier is compared as written.
    """
    text = value.strip()
    if not text:
        return None
    orcid = normalize_orcid(text)
    if orcid:
        return f"orcid:{orcid}"
    email = text[7:] if text.lower().startswith("mailto:") else text
    if _EMAIL_RE.fullmatch(email):
        return f"email:{email.lower()}"
    return f"id:{text.rstrip('/')}"


def _keys_of(item: Any) -> frozenset[str]:
    """Identity keys of a person-like object, or of a bare reference."""
    keys = set()
    if isinstance(item, dict):
        for name in IDENTITY_FIELDS:
            for text in _iter_strings(item.get(name)):
                key = identity_key(text)
                if key:
                    keys.add(key)
    else:
        for text in _iter_strings(item):
            key = identity_key(text)
            if key:
                keys.add(key)
    return frozenset(keys)


def _find_orcid(item: dict[str, Any]) -> str | None:
    for name in ORCID_FIELDS:
        for text in _iter_strings(item.get(name)):
            orcid = normalize_orcid(text)
            if orcid:
                return orcid
    return None


def _public_urls(item: dict[str, Any]) -> list[str]:
    """Absolute http(s) identifiers other than ORCID."""
    urls = []
    for name in DISPLAY_FIELDS:
        for text in _iter_strings(item.get(name)):
            if _HTTP_URL_RE.fullmatch(text) and not normalize_orcid(text):
                urls.append(text)
    return _unique(urls)


# --------------------------------------------------------------------------- #
# Names, affiliations, roles
# --------------------------------------------------------------------------- #


def _display_name(item: dict[str, Any]) -> tuple[str, str, str]:
    """Return (given, family, display name) for a person-like object."""
    given = _text(item.get("givenName"))
    family = _text(item.get("familyName"))
    name = " ".join(part for part in (given, family) if part)
    return given, family, name or _text(item.get("name"))


def _affiliations(item: dict[str, Any]) -> list[str]:
    names = []
    for affiliation in as_list(item.get("affiliation")):
        if isinstance(affiliation, dict):
            name = _text(affiliation.get("name")) or _text(affiliation.get("legalName"))
        else:
            name = _text(affiliation)
        if name:
            names.append(name)
    return _unique(names)


def _role_names(item: dict[str, Any]) -> list[str]:
    names = []
    for key in ROLE_NAME_KEYS:
        names.extend(_text(text) for text in _iter_strings(item.get(key)))
    return _unique(name for name in names if name)


# --------------------------------------------------------------------------- #
# Parsing
# --------------------------------------------------------------------------- #


def _has_person_fields(item: dict[str, Any]) -> bool:
    return any(_text(item.get(k)) for k in ("givenName", "familyName", "name"))


def _parse_person(item: dict[str, Any], where: str, issues: list[str]) -> Person:
    given, family, name = _display_name(item)
    orcid = _find_orcid(item)
    person = Person(
        given=given,
        family=family,
        name=name,
        orcid=orcid,
        urls=_public_urls(item),
        keys=_keys_of(item),
        other_ids=_unique(
            text for name in ("@id", "id") for text in _iter_strings(item.get(name))
        ),
        affiliations=_affiliations(item),
        roles=_role_names(item),
        source=where,
    )
    if not name:
        issues.append(f"{where}: person without a name ({person.label}).")
    if orcid and not orcid_checksum_ok(orcid):
        issues.append(
            f"{where}: ORCID {orcid} has an invalid check digit; "
            "please verify it for typos."
        )
    return person


def _parse_role(item: dict[str, Any], where: str, issues: list[str]) -> list[_RoleLink]:
    keys: set[str] = set()
    for key in ROLE_TARGET_KEYS:
        keys |= _keys_of(item.get(key))
    names = _role_names(item)
    if not keys:
        issues.append(f"{where}: Role without a reference to a person; ignored.")
        return []
    if not names:
        issues.append(f"{where}: Role without a roleName; ignored.")
        return []
    refs = tuple(
        _unique(
            text for key in ROLE_TARGET_KEYS for text in _iter_strings(item.get(key))
        )
    )
    return [_RoleLink(frozenset(keys), name, where, refs) for name in names]


def _parse_field(
    metadata: dict[str, Any], name: str, issues: list[str]
) -> tuple[list[Person], list[_RoleLink]]:
    """Parse the ``author`` or ``contributor`` property."""
    value = metadata.get(name)
    if value is None:
        return [], []
    if not isinstance(value, (dict, list, str)):
        raise CodeMetaError(
            f"Unexpected structure: '{name}' must be an object, a string or a "
            f"list, not {type(value).__name__}."
        )

    people: list[Person] = []
    links: list[_RoleLink] = []
    for index, entry in enumerate(as_list(value)):
        where = f"{name}[{index}]"
        if isinstance(entry, str):
            text = _text(entry)
            if text:
                issues.append(f"{where}: plain text entry, only a name is available.")
                people.append(Person(name=text, source=where))
            continue
        if not isinstance(entry, dict):
            issues.append(
                f"{where}: unsupported entry ({type(entry).__name__}); skipped."
            )
            continue

        types = type_names(entry)
        looks_like_role = (
            not types
            and not _has_person_fields(entry)
            and bool(_role_names(entry))
            and any(key in entry for key in ROLE_TARGET_KEYS)
        )
        if "Role" in types or looks_like_role:
            links.extend(_parse_role(entry, where, issues))
        elif "Person" in types or (not types and _has_person_fields(entry)):
            people.append(_parse_person(entry, where, issues))
        else:
            kind = "/".join(types) or "unknown type"
            label = _text(entry.get("name")) or where
            issues.append(
                f"{where}: {kind} entry '{label}' is not a Person or a Role; skipped."
            )
    return people, links


# --------------------------------------------------------------------------- #
# Identity and merging
# --------------------------------------------------------------------------- #


def same_person(first: Person, second: Person) -> bool:
    """Decide whether two entries describe the same person.

    1. When both have an ORCID, the ORCID alone decides.
    2. Otherwise a shared identifier (ORCID, e-mail, @id...) means same person.
    3. The name is only a fallback, used when at least one of the two entries
       has no identifier at all.
    """
    if first.orcid and second.orcid:
        return first.orcid == second.orcid
    if first.keys & second.keys:
        return True
    if not first.keys or not second.keys:
        name = _fold(first.name)
        return bool(name) and name == _fold(second.name)
    return False


def _merge_into(target: Person, other: Person) -> None:
    """Add to ``target`` what ``other`` knows; ``target`` wins on conflicts."""
    target.given = target.given or other.given
    target.family = target.family or other.family
    target.name = target.name or other.name
    target.orcid = target.orcid or other.orcid
    target.urls = _unique(target.urls + other.urls)
    target.keys = target.keys | other.keys
    target.other_ids = _unique(target.other_ids + other.other_ids)
    target.affiliations = _unique(target.affiliations + other.affiliations)
    target.roles = _unique(target.roles + other.roles)


def _deduplicate(people: list[Person]) -> list[Person]:
    unique: list[Person] = []
    for person in people:
        for existing in unique:
            if same_person(existing, person):
                _merge_into(existing, person)
                break
        else:
            unique.append(person)
    return unique


def load_people(metadata: Any) -> People:
    """Extract authors and additional contributors from a CodeMeta document.

    * Roles are resolved over the whole document, whichever array holds them.
    * Duplicate entries of one person are merged.
    * A person declared as author *and* as contributor is kept once, as an
      author, with the roles/affiliations of both entries.
    * Author order is preserved (it is meaningful); use :func:`sort_people`
      when a sorted list is wanted.

    Raises :class:`CodeMetaError` on an unusable structure. Recoverable
    problems are returned in :attr:`People.issues`.
    """
    if not isinstance(metadata, dict):
        raise CodeMetaError("The CodeMeta document must be a JSON object.")

    issues: list[str] = []
    authors, author_links = _parse_field(metadata, "author", issues)
    contributors, contributor_links = _parse_field(metadata, "contributor", issues)
    links = author_links + contributor_links

    everyone = authors + contributors
    for person in everyone:
        matched = [link for link in links if link.keys & person.keys]
        person.roles = _unique(person.roles + [link.role for link in matched])
        person.other_ids = _unique(
            person.other_ids + [ref for link in matched for ref in link.refs]
        )
    for link in links:
        if not any(link.keys & person.keys for person in everyone):
            issues.append(
                f"{link.source}: Role '{link.role}' refers to a person who is not "
                "declared in 'author' or 'contributor'; ignored."
            )

    authors = _deduplicate(authors)
    additional: list[Person] = []
    for person in _deduplicate(contributors):
        match = next((a for a in authors if same_person(a, person)), None)
        if match is None:
            additional.append(person)
        else:
            _merge_into(match, person)
            issues.append(
                f"{person.label} is declared in both 'author' and 'contributor'; "
                "listed once, as an author."
            )
    return People(authors=authors, contributors=additional, issues=issues)


def display_identifiers(person: Person) -> list[str]:
    """Identifiers to show for a person, as written in the metadata.

    The ORCID is rendered separately (see :attr:`Person.orcid_url`). With an
    ORCID, only the other public http(s) URLs are returned. Without one, the
    person's identifier is whatever the metadata uses to designate them: their
    @id and the ``contributor``/``author`` value of their Role objects (for
    instance an e-mail address). JSON-LD blank nodes and fragments (``_:x``,
    ``#x``) are not identifiers and are skipped.
    """
    candidates = list(person.urls)
    if not person.orcid:
        candidates += person.other_ids

    shown: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        text = candidate.strip()
        if text.lower().startswith("mailto:"):
            text = text[len("mailto:") :]
        if not text or text.startswith(("_:", "#")) or normalize_orcid(text):
            continue
        key = identity_key(text)
        if key not in seen:
            seen.add(key)
            shown.append(text)
    return shown


def sort_people(people: Iterable[Person]) -> list[Person]:
    """Sort by family name, then given name, in a deterministic order."""

    def key(person: Person) -> tuple[str, str, str, str]:
        family = _fold(person.family) or _fold(person.name)
        return (family, _fold(person.given), _fold(person.name), person.orcid or "")

    return sorted(people, key=key)


# --------------------------------------------------------------------------- #
# Markdown helpers
# --------------------------------------------------------------------------- #


def md_escape(value: Any) -> str:
    """Escape text for inline Markdown, including table cells.

    Whitespace and line breaks are collapsed to single spaces, characters that
    Markdown could interpret are backslash-escaped, and ``&`` is escaped only
    where it would start an HTML entity.
    """
    text = " ".join(str(value).split())
    text = _MD_SPECIAL_RE.sub(r"\\\1", text)
    return _MD_ENTITY_RE.sub(r"\\&", text)


def md_link(label: str, url: str) -> str:
    """Build ``[label](url)``; only http(s) URLs become links, else plain text.

    Characters that would break a Markdown link destination (spaces,
    parentheses, angle brackets...) are percent-encoded.
    """
    if not _HTTP_PREFIX_RE.match(url.strip()):
        return md_escape(label)
    safe_url = quote(url.strip(), safe="%:/?#@!$&'*+,;=-._~")
    return f"[{md_escape(label)}]({safe_url})"
