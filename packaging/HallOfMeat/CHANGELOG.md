# Changelog

## 0.1.0

- Package the existing offline Slam / Hall of Meat prototype as a ReSkate mod ZIP.
- Include challenge scoring, X-ray injuries, fractures, effects, bail controls
  and saved-start retries from the Hall of Meat branch.
- Add paired runtime/launcher backup, installation and restore scripts with game
  identity and file-integrity checks.
- Disable automatic binary updates and crash uploads for the custom build.
- Resolve the default game folder after PowerShell parameter binding, fixing
  installation through Install.bat on Windows PowerShell.
- Ship a compact trainer-style package with the required file-verification metadata.
