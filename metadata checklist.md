# Metadata checklist — RFID-M0

This file tracks metadata fields that need to be completed or verified before the next release.

The authoritative software metadata are maintained in `codemeta.json`.

## Pending metadata

- [ ] `datePublished`: set the actual publication date once the release is published.
- [X] `downloadUrl`: verify that the URL points to an actual downloadable release artifact.
- [ ] `softwareRequirements`: verify dependency names, versions, and repository URLs.
- [ ] `maintainer`: explicitly identify the current software maintainer.
- [ ] `funder`: verify which organizations actually funded the software.
- [ ] `isPartOf`: confirm the intended relationship with the MIBE platform.
- [ ] `referencePublication`: add a scientific publication describing the software, if available.
- [ ] `copyrightHolder`: verify the copyright holder, if applicable.

## Release information

- Planned release date: TBD
- Target version: TBD
- Release status: Planned

## Validation before release

- [ ] Validate `codemeta.json` syntax.
- [ ] Check CodeMeta property names and value types.
- [ ] Verify that all URLs are valid and accessible.
- [ ] Confirm that the metadata match the released version.
- [ ] Update `datePublished` only when the release has actually been published.

## Maintenance

Update this checklist whenever metadata requirements change. Remove completed items or retain them as a release history when appropriate.
