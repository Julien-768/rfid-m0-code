
# Metadata scripts

This directory contains the Python utilities used to generate and maintain
file-level contribution metadata and Doxygen source-file headers.

The scripts use the project's `codemeta.json` as the central source of
software metadata and Git history as the source of file-level contributions.

## 1. Directory structure

```text
project-root/
├── codemeta.json
├── contributions.json          # Generated
├── CONTRIBUTIONS.md            # Generated
└── scripts/
    └── metadata/
        ├── README.md
        ├── codemeta_identity.py
        ├── codemeta_people.py
        ├── codemeta_roles.py
        ├── generate_contributions.py
        ├── generate_files.py
        ├── generate_headers.py
        └── patch_generate_files.py
```

## 2. Scripts

### `codemeta_roles.py`

Provides utilities to extract dated `Role` information from CodeMeta metadata.

It associates role periods (`startDate`, `endDate`) with declared people,
using the identity resolution utilities from `codemeta_people.py`.

### `codemeta_identity.py`

Resolves Git identities to the people declared in `codemeta.json`.

The **e-mail address** is the matching key between a Git author and a
CodeMeta person (`author`, `contributor` or `maintainer`). The names written
in generated files are the `givenName` and `familyName` of CodeMeta, exactly
as spelled there, never the Git author name (which may be a nickname, an
alias or an abbreviation).

It also returns the full name of the `copyrightHolder`.

### `generate_contributions.py`

Analyses Git history and generates:

- `CONTRIBUTIONS.md`: human-readable contribution information.
- `contributions.json`: structured file-level contributor information.

Contributors are matched to `codemeta.json` by e-mail address. The CodeMeta
name and affiliation replace the Git name; the Git names seen for that
e-mail address are kept as aliases (`gitNames` in the JSON, "Git author
names" in the Markdown). A Git e-mail address that matches nobody in CodeMeta
keeps its Git name and a warning is printed on stderr.

The generated JSON is used by the header generator.

The script reads the whole Git history (`git log --all`). In a shallow clone
the contributions are incomplete: fetch the full history first.

### `generate_headers.py`

Generates Doxygen headers for:

- C and C++ files (`.c`, `.h`, `.cpp`, `.hpp`, `.cc`): a `/** ... */`
  comment at the top of the file.
- Python files (`.py`): SPDX comment lines after the shebang and encoding
  declaration, followed by a Doxygen module docstring.

The generated headers include:

- `@file` and the existing description (see below).
- Software version and metadata date.
- Global authors from `codemeta.json`.
- Project name and description.
- File-specific contributors from `contributions.json`.
- Copyright and license information, including SPDX identifiers.

Authors and contributors use the same layout: first name and last name from
CodeMeta, then the affiliation and the e-mail address.

```text
@section authors Authors
- Julien Courtecuisse
  Affiliation: IPHC-DEPE; UMR 7178; ...
  Email: julien.courtecuisse@iphc.cnrs.fr
```

The `@section license` line cites the `copyrightHolder` of CodeMeta in full
(`givenName familyName`, or `name` for an organisation).

Global authors are kept separate from file-level contributors.

An author already declared in CodeMeta is not repeated in the
file-specific contributor section.

### `patch_generate_files.py`

Runs the metadata generation workflow in the correct order:

1. Generate contribution metadata from Git history.
2. Generate or check source-file headers.

## 3. Requirements

- Python 3.10 or later.
- Git installed and available in `PATH`.
- A Git repository with accessible commit history.
- A valid `codemeta.json` at the repository root.
- `codemeta_people.py` available in this directory when using
  `codemeta_roles.py`.

The scripts use only the Python standard library, except for the
`codemeta_roles.py` dependency on `codemeta_people.py`.

No additional Python packages are required for the metadata scripts.

The REUSE command-line tool is required for license compliance checks.

## 4. Usage

All commands must be executed from the repository root.

### Generate contribution metadata

```bash
python scripts/metadata/generate_contributions.py
```

Optional date filters:

```bash
python scripts/metadata/generate_contributions.py \
    --since 2025-01-01 \
    --until 2026-12-31
```

Custom output paths:

```bash
python scripts/metadata/generate_contributions.py \
    --json contributions.json \
    --markdown CONTRIBUTIONS.md
```

### Generate Doxygen headers

```bash
python scripts/metadata/generate_headers.py
```

Check headers without modifying files:

```bash
python scripts/metadata/generate_headers.py --check
```

### Run the complete workflow

Generate contributions and headers:

```bash
python scripts/metadata/patch_generate_files.py
```

Check headers without modifying source files:

```bash
python scripts/metadata/patch_generate_files.py --check
```

Skip contribution regeneration:

```bash
python scripts/metadata/patch_generate_files.py \
    --skip-contributions
```

## 5. Metadata sources

### Global project metadata

`codemeta.json` is the authoritative source for:

- Software name.
- Software description.
- Software version.
- Publication and modification dates.
- Global authors.
- Copyright holders.
- License information.

The date used in source headers follows this priority:

1. `dateModified`
2. `datePublished`
3. `dateCreated`

### File-level contributions

`contributions.json` is generated from Git history.

It associates each source file with the Git authors who modified it.

The generated contribution data is not a replacement for the
global author and contributor metadata in CodeMeta.

## 6. Contribution attribution

File-level attribution is based on Git commit authorship.

This means that a contributor is associated with a file when Git history
contains a commit authored by that person that modified the file.

The current implementation has some limitations:

- Git author names and email addresses may vary between commits. Names are
  taken from CodeMeta, so a person who commits with an e-mail address that is
  not declared in `codemeta.json` appears under the Git name.
- Different email addresses belonging to the same person are not
  automatically merged: declare each address in CodeMeta or use a Git
  `.mailmap`.
- File renames may require additional handling.
- Git history may not contain all contributions, especially when files
  were imported from another repository.
- Commit authorship does not necessarily represent every form of
  scientific or technical contribution.

Review `CONTRIBUTIONS.md` and `contributions.json` before using the
generated attribution in published source files.

## 7. Doxygen header policy

The generated headers distinguish between:

### Global authors

People declared in the `author` property of `codemeta.json`.

### File-level contributors

People identified in the Git history for the specific file.

Global authors are not duplicated in the file-level contributor section.

The generated header also includes the project version, date, description,
copyright and SPDX license identifier. The SPDX fields are generated from
CodeMeta, not from Git contribution history.

The existing description is preserved when updating a file: `@brief`, the
detailed text and tags such as `@note`, `@warning`, `@ingroup` or `@code`
blocks are kept. Hand-written `@author`, `@version` and `@date` lines are
replaced by the generated ones.

Existing top-of-file comments are handled as follows:

- A `/** ... */` comment containing `@file` is the header and is rewritten.
- A `/* name.ext */` comment that only repeats the file name is removed.
- A `/* name.ext - description */` comment becomes the `@brief`.
- Any other comment is left untouched, below the new header.
- In Python, the module docstring (or a leading `#` comment block followed by
  a blank line) becomes the `@brief` and the details.

## 8. SPDX identifiers and REUSE compatibility

Every generated source file must carry machine-readable SPDX metadata:

- `SPDX-FileCopyrightText` identifies the copyright holder(s) declared in
  `codemeta.json`, together with the applicable copyright year.
- `SPDX-License-Identifier` contains the SPDX license expression declared
  by CodeMeta (for this project: `GPL-3.0-or-later`).

The SPDX copyright holder is taken from CodeMeta's `copyrightHolder`
property. It must not be inferred from `author`, `contributor`,
`contributions.json` or Git commit authorship.

File-level Git contributors are not automatically copyright holders.

### C and C++ files

Place the SPDX fields inside the generated Doxygen block comment, for example:

```cpp
/**
 * @file example.cpp
 * @brief Example source file.
 *
 * SPDX-FileCopyrightText: 2022 Julien Courtecuisse
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See the LICENSE file for the complete license text.
 */
```

### Python files

Keep the shebang and encoding declaration at the beginning of the file.

Place SPDX metadata as Python comments immediately after that prologue and
before the Doxygen module docstring:

```python
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# SPDX-FileCopyrightText: 2022 Julien Courtecuisse
# SPDX-License-Identifier: GPL-3.0-or-later

"""
@file example.py
@brief Example Python module.
...
"""
```

The SPDX lines must remain comments outside the module docstring. This lets
REUSE detect the file-level licensing information while preserving the
module docstring as the first Python statement.

The Python header parser and renderer must preserve these SPDX comments on
every generation pass. They must not absorb them into the Doxygen description
or move them into the docstring.

The full license text may remain in the Doxygen header for human readers.
The SPDX identifier is the authoritative machine-readable license
declaration and must match the repository's `LICENSE` file and CodeMeta.

### Validation

After generating headers, run REUSE's linter from the repository root:

```bash
reuse lint
```

Resolve all missing or inconsistent copyright and license information
reported by the linter.

Also check that the header generator is idempotent:

```bash
python scripts/metadata/patch_generate_files.py
python scripts/metadata/patch_generate_files.py --check
reuse lint
git diff --check
git diff
```

The second generation in check mode should report that all headers are up to
date.

Review the diff before committing. Do not add `SPDX-FileContributor`
automatically from Git history.

## 9. Recommended workflow

After modifying source files:

1. Review the Git history.
2. Regenerate contribution metadata.
3. Review `CONTRIBUTIONS.md`.
4. Check the generated `contributions.json`.
5. Generate Doxygen headers.
6. Run `reuse lint`.
7. Review the changes with Git.
8. Commit the generated metadata and source headers.

Example:

```bash
python scripts/metadata/patch_generate_files.py

reuse lint

git status
git diff -- CONTRIBUTIONS.md contributions.json
git diff -- '*.c' '*.h' '*.cpp' '*.py'
```

To check whether headers are up to date:

```bash
python scripts/metadata/patch_generate_files.py --check
```

## 10. Important precautions

The scripts modify generated files and source-file headers.

Always review the resulting Git diff before committing.

Do not use generated Git attribution as an automatic substitute for
scientific authorship decisions or formal contributor recognition.

The metadata scripts do not modify `codemeta.json` or push changes
to the remote repository.

## 11. License

These scripts are part of the RFID-M0 project.

Their licensing follows the license declared in the project's
`codemeta.json` and the repository's license file.


## Section à incorporer  pas encore automatisé pour reuse.toml et generate_reuse_headers.py

Metadata and licensing

The metadata scripts rely on the project's metadata files, particularly codemeta.json, to maintain consistent author, copyright and licensing information.

The project uses the REUSE Specification to document copyright and licensing information.

REUSE integration is not yet fully automated.

In particular:

generate_reuse_headers.py does not currently generate or update REUSE.toml.

The consistency between REUSE.toml and the SPDX headers generated by the scripts must therefore be maintained manually.

Files that cannot conveniently contain SPDX headers, such as binary files and JSON files, require appropriate annotations in REUSE.toml.

The REUSE configuration and its annotations must be checked independently using:

reuse lint

Usage

Run the scripts from the repository root unless otherwise specified.

For example:

python scripts/metadata/generate_headers.py
python scripts/metadata/generate_reuse_headers.py

Check the available command-line options of each script before running it:

python scripts/metadata/generate_headers.py --help
python scripts/metadata/generate_reuse_headers.py --help

Maintenance

When modifying project metadata or licensing information:

Update the relevant metadata files.

Run the metadata generation scripts.

Review the generated changes.

Check the REUSE annotations and REUSE.toml manually.

Run reuse lint to verify compliance.

## Section TODO, liste des choses à améliorer pour la prochaine version des scripts, à incorporer

Pour les fichiers python, ne pas écrire sur plus de 100 caractères pour ruff / flake8
