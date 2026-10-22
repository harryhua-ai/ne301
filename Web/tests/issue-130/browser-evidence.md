# Issue 130 browser verification

Browser: Playwright CLI with Chromium at `http://127.0.0.1:5173/login`.

The local browser route for `POST /api/v1/login` returned a controlled `401 Unauthorized` JSON response. No device backend or real credential was used. The browser network record omits request bodies and credentials.

## Before the change

- Focused the password field, entered a synthetic valid-format value, and pressed Enter with the real browser keyboard. No `POST /api/v1/login` was emitted; the page stayed at `/login`.
- Clicked Login. One `POST /api/v1/login` was emitted.

## Candidate

- Focused the password field, pressed Tab to focus the visibility button, then pressed Enter. The field changed to visible text and no login request was emitted.
- Refocused the password field and pressed Enter with the real browser keyboard. One `POST /api/v1/login` returned the controlled `401 Unauthorized`; the page stayed at `/login` with no document navigation or native GET submission.
- Clicked Login. One additional `POST /api/v1/login` returned the same `401 Unauthorized` and the page stayed at `/login`.
- The captured request list showed exactly one login POST for each submit action. Other listed requests were unrelated startup calls.

Screenshot: [login-enter-click.png](login-enter-click.png). The synthetic password is masked in the screenshot.

## Build and test evidence

- `pnpm install`: PASS.
- `pnpm build`: PASS, exit code 0; 1905 modules transformed. Existing warnings noted outdated browser data and a minified chunk above 500 kB.
- `pnpm test`: FAIL, 70/71 tests passed. The unchanged `src/test/h264Player.test.ts:53` Annex-B assertion failed; the isolated `pnpm exec vitest run src/test/h264Player.test.ts` reproduced the same failure. The new `src/test/login.test.tsx` passed all 12 tests.
- SHA-256 `src/pages/login/index.tsx`: `2fd40ad3579b22743bf01d90db0b00ea4f84584130ec5659d843a1d47dd764de`.
- SHA-256 `src/test/login.test.tsx`: `b889b730e664507bc8316bef41e33e2ea4db2686548684540cecbb8f4711f0a7`.
- Candidate preservation and delivery were not run because the required full test gate is not green.
