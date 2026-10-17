# Host report purge coordination for native uninstall

Status: software candidate for Issue #101. Device storage, network, flash, and product acceptance remain NOT_RUN for #48/#58.

## Coordinates

- Base: `9c22549d42dcfe0f4e4ae60fbc434c417e5a93de`.
- Host #83 lifecycle merge: `fdefbc67abf13dbdbebbd71a89967d20e307486f`.
- Host #88 trusted TBS-swap guard: `8e76949503b804f76843c021c375e7c9657d12a1`.
- The #88 report implementation remains unchanged. The adapter uses only the public declarations in `app_report_service.h`.

## Trust and ordering

`app_install_remove_handler` reaches the new path only after administrator authentication and `confirm_delete_data: true`. It calls `app_report_service_device_uninstall_prepare` before `app_install_uninstall_confirmed`. A prepare failure returns `refused` and skips #83 local deletion. After the coordinator returns, the handler always calls `app_report_service_device_uninstall_finish`.

The Host adapter reads the committed install record or the existing #83 durable uninstall record. Before deleting the committed record, it writes a CRC-protected `app_report_cleanup_intent` containing the complete encoded install record and a monotonic cleanup generation. The generation counter is stored and read back in the same installer transaction. Retry reuses the same prepared record and generation; a different pending record, invalid CRC, failed readback, or unknown state fails closed. Reinstalling the same native TBS gets a fresh generation after the earlier purge is complete.

The adapter builds report identity from that Host-owned record, including native TBS. For `trust_uninstall_target`, it supplies a committed-IO view that hides the #83 uninstall/pending keys and uses the durable record only when the actual committed key is absent after #83 local cleanup. If an actual committed record exists, it remains authoritative and must match the entire encoded record. This preserves #88's committed/full-TBS check while allowing the post-delete callback to prove the same durable target.

Before local deletion, the adapter queries report policy, calls the trusted uninstall seam, commits both MQTT and Webhook disabled with the current revision, re-queries the committed status, cross-checks report `status` against each queue entry, and clears the active report session. The session clear closes report admission during the uninstall window. The real #83 cleanup callback then reloads the same durable context, revalidates it against the #83 pending record and committed-IO view, calls `app_report_service_policy_purge`, and requires an exact matching `app_report_service_policy_purge_status` with COMPLETE, `last_result == OK`, and verified storage. Only then does the callback return CONFIRMED so #83 can remove its uninstall intent.

The report service's existing recursive report lock serializes the Host context and report calls; no report API is called while an installer transaction is held. The purge callback closes the report session and restores the raw committed IO view before releasing that lock, including after a temporary policy-query session restore. Report queue state and policy remain separate from MQTT/Webhook service configuration. The adapter does not modify topics, URLs, credentials, or global network settings.

## Real integration fixture

`tests/app_report_uninstall/test_app_report_uninstall.c` links the actual `app_installer.c` translation unit, which includes the #83 lifecycle, with the actual `app_report_service.c`, `app_report_service_device.c`, and `app_report_policy.c`. The #83-to-#88 call site is the `dev_report_purge` cleanup callback passed to `app_install_uninstall_confirmed`; it invokes `app_report_service_trust_uninstall_target`, policy query/commit, policy purge, and purge status.

The C fixture covers a report queued before policy query, status cancellation and admission closure, actual current-B uninstall, same-base A/B native-TBS replacement, stale A uninstall refusal, self-declared old-TBS query rejection, started-UNKNOWN send refusal, policy storage read error and partial write, corrupt durable intent, cleanup-key write failure, retry after simulated service/installer restart, and same-TBS reinstall with a new cleanup generation. Purge success uses the real report service and policy store; injected UNKNOWN is used only to exercise #83's retry branch.

The `app_report` host test Makefile now links `app_state_store.o`, which the current installer/lifecycle translation unit requires.

## Commands and results

Run from the repository root:

```sh
python3 Script/version_header.py --fsbl-version 1.0.3.1 --stedgeai-version 3.0.0
make app
make -C tests/app_report_uninstall test
make -C tests/app_report test
make -C tests/app_admission test
make -C tests/app_host_control test
make -C tests/app_host_run test
make -C tests/app_host_session test
make -C tests/app_installer test
make -C tests/app_state test
make -C tests/app_sidecar test
make -C tests/app_sidecar_lifecycle test
make -C tests/app_host test
```

Observed host results: new integration 130 checks; report 552; report policy 575; native admission v1/v2/v3 179 + 84 + 61 and admission gate 97; Host control 441 + 107; Host run 214; Host session 540; installer 271 + 280 + 173 + 54; state 188 + 260; native Sidecar 155; Sidecar lifecycle 351; Host validate 21. `make app` completed successfully with the repository's default EdgeAI version selected by the root build.

The issue's named `web_sidecar_assets` and `web_sidecar_bridge` suites are not present in this checkout (`git ls-files` has no matching suite or command), so they are NOT_RUN. No firmware was flashed; NVS/LittleFS durability on-device, real MQTT/Webhook behavior, reset/power-cut behavior on hardware, and user product acceptance remain NOT_RUN for #48/#58.
