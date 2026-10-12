---
title: app_admission Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# app_admission Endpoints



Source: [`Custom/Services/Web/api/api_app_admission_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_admission_module.c)

**2** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `POST` | `/api/v1/app/admission/verify` | ✅ | `app_admission_verify_handler` |
| `GET` | `/api/v1/app/admission/state` | ✅ | `app_admission_state_handler` |
