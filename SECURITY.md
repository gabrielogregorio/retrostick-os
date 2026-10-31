# Security Policy

  RetroStick OS runs entirely from the SD card and never writes to the stick's SPI flash. Remove the card and the stick goes back to its factory menu. That guarantee is the core of this
  project, so anything that breaks it is treated as a serious issue.

  ## Supported Versions

  Only the latest release gets security fixes. There are no backports.

  | Version | Supported          |
  | ------- | ------------------ |
  | 1.0.x   | :white_check_mark: |
  | < 1.0   | :x:                |

  ## Reporting a Vulnerability

  Do not open a public issue, pull request or discussion for a security problem.

  Report it privately through the **Security** tab of this repository, using **Report a vulnerability**. Include:

  - The affected version, or the commit if you built from source
  - Where it happens: the stick, the PC build, or the tools in `tools/`
  - The steps to reproduce it and what you saw happen
  - A minimal test file, if one is needed

  Do not attach commercial ROMs or factory firmware dumps. Use a small crafted file instead, like the invented games in `tests/mock/`.

  ## What to Expect

  This project is maintained in spare time, so these are targets and not guarantees.

  | Step                | Target                  |
  | ------------------- | ----------------------- |
  | First response      | Within 7 days           |
  | Triage decision     | Within 14 days          |
  | Status updates      | Every 14 days until closed |

  **If the report is accepted**, the fix ships in a new release and you are credited in the advisory, unless you prefer to stay anonymous. Please keep the details private until that release
  is out.

  **If the report is declined**, you get the reason. If the problem belongs to another project, you get a pointer to where it should be reported.

  ## Scope

  **In scope:**

  - Anything that writes to the SPI flash, or that makes the stick stop returning to its factory menu without the card
  - Data loss on the card caused by this project, beyond the known factory firmware behavior listed in the README
  - Memory corruption or code execution in the menu or the runner, triggered by files read from the card, such as `.meta` files, `favorites.txt`, `menu-state.txt`, `launch.txt` or save
  states
  - The boot script `card-base/retro/init`
  - The tools in `tools/`, including cover downloads with `gen_covers.py --download`
  - The build process and the GitHub Actions workflows

  **Out of scope:**

  - Bugs in emulator cores. They are fetched separately, each from its own project, so report them upstream
  - The factory firmware, bootloader and kernel on the stick
  - Attacks that need the stick opened or direct access to the SPI flash
  - Malicious files on a card that the owner chose to trust. The card holds the boot script, so whoever controls the card controls what runs on the stick
  - Sticks with other boards, which this project does not target

  ## Good to Know

  Treat any card or game pack from an unknown source as untrusted code. Check that `retro/init` is the one from this repository, and that there is no file named `SPI_ROM.bin` on the card:
  the factory firmware rewrites the flash from it.

  ## Safe Testing

  Good-faith research is welcome. Test only on hardware and cards that you own, and do not write to the SPI flash to prove a finding. Read-only access with `tools/re/read_spi.sh` is enough.

