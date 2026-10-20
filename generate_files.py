import json


def generate_authors_file(codemeta_data, output_file="AUTHORS.md"):
    """Génère un fichier AUTHORS.md à partir des données codemeta.json."""
    authors = codemeta_data.get("author", [])
    contributors = codemeta_data.get("contributor", [])

    with open(output_file, "w", encoding="utf-8") as f:
        f.write("# List of Authors and Contributors\n\n")

        if authors:
            f.write("## Main Authors\n\n")  # Ligne vide ajoutée
            for author in authors:
                name = f"{author.get('givenName', '')} {author.get('familyName', '')}"
                orcid = author.get("orcid", "Not provided")
                affiliation = author.get("affiliation", {}).get("name", "Not provided")
                roles = ", ".join(author.get("contributorType", []))
                f.write(f"- **{name}** (ORCID: [{orcid}](https://orcid.org/{orcid}))\n")
                f.write(f"  *Affiliation*: {affiliation}\n")
                f.write(f"  *Role*: {roles}\n\n")

        if contributors:
            f.write("## Contributors\n\n")  # Ligne vide ajoutée
            for contributor in contributors:
                name = f"{contributor.get('givenName', '')} {contributor.get('familyName', '')}"
                orcid = contributor.get("orcid", "Not provided")
                affiliation = contributor.get("affiliation", {}).get(
                    "name", "Not provided"
                )
                roles = ", ".join(contributor.get("contributorType", []))
                f.write(f"- **{name}** (ORCID: [{orcid}](https://orcid.org/{orcid}))\n")
                f.write(f"  *Affiliation*: {affiliation}\n")
                f.write(f"  *Role*: {roles}\n\n")


def generate_citation_file(codemeta_data, output_file="CITATION.cff"):
    """Génère un fichier CITATION.cff à partir des données codemeta.json."""
    authors = codemeta_data.get("author", [])
    version = codemeta_data.get("version", "1.0.0")
    name = codemeta_data.get("name", "Project Name")
    date_published = codemeta_data.get("datePublished", "2026-02-23")
    license_info = codemeta_data.get("license", {})
    license_name = license_info.get("name", "Not provided")
    license_description = license_info.get("description", {}).get(
        "en", "No description available."
    )
    identifier = (
        codemeta_data.get("@id", "").replace("https://doi.org/", "")
        if codemeta_data.get("@id")
        else "Not provided"
    )

    with open(output_file, "w", encoding="utf-8") as f:
        f.write(f"""cff-version: 1.2.0
message: "If you use this software, please cite it as follows."
title: "{name}"
authors:
""")
        for author in authors:
            given_name = author.get("givenName", "")
            family_name = author.get("familyName", "")
            orcid = author.get("orcid", "Not provided")
            f.write(f"""  - given-names: "{given_name}"
    family-names: "{family_name}"
    orcid: "https://orcid.org/{orcid}"
""")
        f.write(f"""
version: "{version}"
doi: "{identifier}"
date-released: "{date_published}"
license:
  name: "{license_name}"
  description: "{license_description}"
""")


def main():
    # Charger le fichier codemeta.json
    with open("codemeta.json", "r", encoding="utf-8") as f:
        codemeta_data = json.load(f)
        print(codemeta_data)  # Affiche les données pour vérification

    # Générer les fichiers
    generate_authors_file(codemeta_data)
    generate_citation_file(codemeta_data)
    print("Files AUTHORS.md and CITATION.cff generated successfully!")


if __name__ == "__main__":
    main()
