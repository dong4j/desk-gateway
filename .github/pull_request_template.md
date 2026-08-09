## Summary

<!-- Explain the problem and the observable outcome of this change. -->

## Related issue

<!-- Use "Closes #123" when applicable. Write "None" for a small standalone change. -->

## Scope and boundaries

- In scope:
- Explicitly not included:
- Verified facts vs remaining assumptions:

## Verification

- [ ] `./scripts/check-firmware.sh` passes, or this change does not affect firmware.
- [ ] Relevant documentation has been updated, or no documentation change is needed.

| Check | Result / evidence | Not run reason |
|-------|-------------------|----------------|
| Automated build / checks | | |
| Real desk movement and stop | | |
| On-device Web / Wi-Fi behavior | | |
| Protocol capture or driver evidence | | |

<!-- Use "N/A" for checks outside the change scope. Do not claim hardware verification that was not performed. -->

## Safety and security

- [ ] Motion timeout, stop behavior, and physical safety paths are not weakened.
- [ ] No credentials, private network data, build artifacts, or generated secrets are included.
- [ ] Protocol changes identify known behavior separately from unknown or inferred behavior.
- [ ] I have read and will follow the repository Code of Conduct.
