# Toka 0.12.0 release notes — candidate draft

These notes describe the planned release. Q0 qualification and public publication
have not run; the candidate remains Preview until the release prerequisites close.

0.12 adds project-oriented `toka test`, explicit locked-version/digest feedback from
`toka add`, and scoped semantic evidence after the unchanged full compiler check.
Manager evidence consumers use `toka.semantic-evidence-view` v1; compiler public
evidence v1 stays unchanged.

The blocking SDK platforms are Linux x64, Linux ARM64 and macOS ARM64. Intel Mac
is independent best-effort support. Its binary is included only after the exact
same-candidate package passes basic validation; a release without that package
requires source compilation. Existing 0.11 four-platform evidence is unchanged.

Before publication, replace this candidate-status paragraph with the accepted Q0
and replay links and list exactly the assets approved for this release.
