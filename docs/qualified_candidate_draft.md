# Create a draft from an already qualified candidate

The `Create Draft From Qualified Candidate` manual workflow is a release-control
entry, separate from the immutable SDK source revision. It reuses a successful
`Release Qualification` candidate dispatch and its original four archives. It
never invokes a compiler, rebuilds an archive, or relaxes the four-target gate.

For the first 0.11.0 release, the prepared inputs are:

- `tag_name`: `v0.11.0`
- `candidate_sha`: `57b0f7dd7d52bdc24c6dde0457803240e5c62e8a`
- `qualification_run_id`: `36844946549`

The release-control workflow must first be reviewed and made available on the
default branch. Its controller SHA is recorded by Actions and is **not** the SDK
candidate SHA. Do not move the candidate tag to that controller revision or
claim the new release-control code received the candidate's platform tests.

## Preconditions and sequence

1. Check the source run's repository, workflow path, successful completion,
   dispatch event and exact candidate SHA.
2. Download all four original candidate-archive artifacts and release-gate
   evidence. Run the candidate's unchanged qualification verifier against all
   thirteen stages and TaskHandle/cancellation receipts.
3. Require the exact four artifact directories and archive names. Copy the
   original bytes into a fresh staging directory, compare hashes and generate
   `SHA256SUMS`. Save preparation evidence before changing any remote state.
4. Refuse any existing release. Create an annotated tag using the workflow's
   repository `GITHUB_TOKEN`, or verify that an existing annotated tag directly
   names the candidate commit. Never replace or move a tag.
5. Create an unpublished, non-prerelease draft with the four original archives
   and `SHA256SUMS`, explicitly leaving Latest unchanged. Release notes come
   from the candidate commit, not the controller checkout.
6. Download the draft assets and compare every archive and checksum with the
   original qualification artifacts. Save the annotated tag and draft receipt.
   Any mismatch fails the workflow; it cannot proceed to public promotion.

A personal token, App token or ordinary tag push must not replace `GITHUB_TOKEN`
in this entry. [GitHub's documented event suppression](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)
prevents its tag creation from triggering the candidate's tag-push qualification
workflow. The historical tag-push release path remains available and unchanged;
this explicit qualified-candidate path avoids the duplicate build.

## Recovery and publication

An existing annotated tag is accepted only if its direct commit target matches;
lightweight, nested, or mismatched tags fail. A failed run may leave a tag without
a draft; a retry may reuse that exact tag. A draft or release already present is
never overwritten. If draft upload or readback fails, its partial state requires
review and explicit recovery; it is not publishable merely because it exists.

This entry does not dispatch replay or publish. After draft verification and
separate authorization, run `Qualified macOS x64 Artifact Replay` using the same
candidate and qualification run, `asset_source=candidate_run`, and the recorded
Intel archive SHA-256. The existing protected `Promote Verified Draft Release`
then verifies the original four archives against downloaded draft bytes, the
qualified replay receipt and the candidate identity before public publication
and an explicit Latest update. No replay or promotion checks are removed.

Local contract tests and real-artifact verification prepare this entry for
review. They do not prove a hosted draft creation succeeded. Creation of the
annotated tag and draft remains a separately authorized operation.
