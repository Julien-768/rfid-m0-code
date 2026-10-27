# Metadata checklist — RFID-M0

This file tracks metadata fields that need to be completed or verified before the next release.

The authoritative software metadata are maintained in `codemeta.json`.

## Pending metadata

- [-] `datePublished`: set the actual publication date once the release is published.
- [X] `downloadUrl`: verify that the URL points to an actual downloadable release artifact.
- [X] `softwareRequirements`: verify dependency names, versions, and repository URLs.
- [X] `maintainer`: explicitly identify the current software maintainer.
- [X] `funder`: verify which organizations actually funded the software.
- [X] `isPartOf`: confirm the intended relationship with the MIBE platform.
- [O] `referencePublication`: add a scientific publication describing the software, if available.
- [X] `copyrightHolder`: verify the copyright holder, if applicable.

## Release information

- Planned release date: TBD
- Target version: TBD
- Release status: Planned

## Validation before release

- [ ] Validate `codemeta.json` syntax.
- [ ] Check CodeMeta property names and value types.
- [X] Verify that all URLs are valid and accessible.
- [ ] Confirm that the metadata match the released version.
- [ ] Update `datePublished` only when the release has actually been published.
- [X] Le lien pointe buildInstructions vers une page plus précise si elle existe.

## Maintenance

Update this checklist whenever metadata requirements change. Remove completed items or retain them as a release history when appropriate.

## Git helper

git log --all --author="gitlabalias*" --since="2000-01-01" --pretty=format:"%h - %an, %ad : %s" --date=short

git log --all --author="laesser*" --since="2000-01-01" --pretty=format:"%h - %an, %ad : %s" --date=short

1. Liste simple des noms + emails (uniques)
git log --pretty=format:"%an <%ae>" | sort -u

2. Liste avec nombre de commits par contributeur
git shortlog -s -n
