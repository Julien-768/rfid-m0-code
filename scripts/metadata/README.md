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
        ├── codemeta_roles.py
        ├── generate_contributions.py
        ├── generate_headers.py
        └── patch_generate_files.py
```

## 2. Scripts

### `codemeta_roles.py`

Provides utilities to extract dated `Role` information from CodeMeta metadata.

It associates role periods (`startDate`, `endDate`) with declared people,
using the identity resolution utilities from `codemeta_people.py`.

### `generate_contributions.py`

Analyses Git history and generates:

- `CONTRIBUTIONS.md`: human-readable contribution information.
- `contributions.json`: structured file-level contributor information.

The generated JSON is used by the header generator.

### `generate_headers.py`

Generates Doxygen headers for C and header files (`.c` and `.h`).

The generated headers include:

- Software version and metadata date.
- Global authors from `codemeta.json`.
- Project name and description.
- File-specific contributors from `contributions.json`.
- Copyright and license information.

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

No additional Python packages are required.

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

- Git author names and email addresses may vary between commits.
- Different email addresses belonging to the same person are not
  automatically merged.
- File renames may require additional handling.
- Git history may not contain all contributions, especially when files
  were imported from another repository.
- Commit authorship does not necessarily represent every form of
  scientific or technical contribution.

Review `CONTRIBUTIONS.md` and `contributions.json` before using the
generated attribution in published source files.

## 7. Doxygen header policy

The generated headers distinguish between:

**Global authors**

People declared in the `author` property of `codemeta.json`.

**File-level contributors**

People identified in the Git history for the specific file.

Global authors are not duplicated in the file-level contributor section.

The generated header also includes the project version, date, description,
copyright and SPDX license identifier.

The existing `@brief` description is preserved when updating a file.

## 8. Recommended workflow

After modifying source files:

1. Review the Git history.
2. Regenerate contribution metadata.
3. Review `CONTRIBUTIONS.md`.
4. Check the generated `contributions.json`.
5. Generate Doxygen headers.
6. Review the changes with Git.
7. Commit the generated metadata and source headers.

Example:

```bash
python scripts/metadata/patch_generate_files.py

git status
git diff -- CONTRIBUTIONS.md contributions.json
git diff -- '*.c' '*.h'
```

To check whether headers are up to date:

```bash
python scripts/metadata/patch_generate_files.py --check
```

## 9. Important precautions

The scripts modify generated files and source-file headers.

Always review the resulting Git diff before committing.

Do not use generated Git attribution as an automatic substitute for
scientific authorship decisions or formal contributor recognition.

The metadata scripts do not modify `codemeta.json` or push changes
to the remote repository.

## 10. License

These scripts are part of project TO COMPLETE.

Their licensing follows the license declared in the project's
`codemeta.json` and the repository's license file.
