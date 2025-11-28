# Tasks: Azure Deployment with Test Environment

**Feature**: 001-azure-deployment-test  
**Input**: spec.md, plan.md, research.md (plan Phase 0)  
**Organization**: Tasks organized by user story to enable independent implementation and testing

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1-US6)
- File paths are absolute from repository root

---

## Phase 1: Setup (Project Infrastructure)

**Purpose**: Basic project structure and script scaffolding

- [ ] T001 Create scripts directory structure: `scripts/`, `scripts/common.sh`
- [ ] T002 Create test directory structure: `test/e2e/`, `test/e2e/fixtures/`
- [ ] T003 [P] Create Azure configuration directory: `azure/`, `azure/.gitignore`
- [ ] T004 [P] Document prerequisites in `specs/001-azure-deployment-test/quickstart.md`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story implementation

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T005 Verify Docker buildx available and QEMU configured for multi-arch
- [ ] T006 Verify Azure CLI authenticated and subscription accessible
- [ ] T007 Verify Azure Container Registry access: `az acr login --name protogatedevacr`
- [ ] T008 Create shared utilities in `scripts/common.sh`: logging, error handling, JSON output
- [ ] T009 Create script argument parser in `scripts/common.sh`: --env, --dry-run, --json, --verbose

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 2 - Build and Push Multi-Arch Docker Images (Priority: P0) 🎯 MVP

**Goal**: Production-ready Docker images for AMD64 and ARM64 architectures

**Independent Test**: 
```bash
./scripts/build-and-push.sh v1.0.0
az acr repository show-tags -n protogatedevacr --repository protogate-server
# Expected: Tags v1.0.0-amd64, v1.0.0-arm64, v1.0.0, latest
```

### Implementation for User Story 2

- [ ] T010 [P] [US2] Create buildx builder setup function in `scripts/build-and-push.sh` (lines 1-50)
- [ ] T011 [P] [US2] Add Docker prerequisite validation in `scripts/build-and-push.sh` (lines 51-100)
- [ ] T012 [US2] Implement multi-arch build logic in `scripts/build-and-push.sh` (lines 101-180)
- [ ] T013 [US2] Add ACR push and manifest list creation in `scripts/build-and-push.sh` (lines 181-220)
- [ ] T014 [US2] Add image verification and size reporting in `scripts/build-and-push.sh` (lines 221-270)
- [ ] T015 [US2] Add dry-run mode and JSON output support in `scripts/build-and-push.sh` (lines 271-300)
- [ ] T016 [US2] Verify Dockerfile.alpine multi-stage build optimization in `docker/Dockerfile.alpine`
- [ ] T017 [US2] Test script: Build AMD64 image locally and verify size <100MB
- [ ] T018 [US2] Test script: Build ARM64 image locally and verify size <100MB
- [ ] T019 [US2] Test script: Push to ACR and verify manifest list created

**Checkpoint**: Docker images buildable and pushable to ACR - can deploy to Azure

---

## Phase 4: User Story 4 - TLS Certificate Management (Priority: P0)

**Goal**: Azure Key Vault provisioned with TLS certificates for secure connections

**Independent Test**:
```bash
./scripts/provision-keyvault.sh --env test
az keyvault secret show --vault-name protogate-test-kv-* --name tls-cert
# Expected: Certificate PEM content returned
```

### Implementation for User Story 4

- [ ] T020 [P] [US4] Create certificate generation script in `scripts/generate-test-cert.sh` (complete file ~50 lines)
- [ ] T021 [P] [US4] Create resource group provisioning in `scripts/provision-keyvault.sh` (lines 1-50)
- [ ] T022 [US4] Add unique Key Vault name generation in `scripts/provision-keyvault.sh` (lines 51-80)
- [ ] T023 [US4] Implement Key Vault creation in `scripts/provision-keyvault.sh` (lines 81-120)
- [ ] T024 [US4] Add certificate upload logic in `scripts/provision-keyvault.sh` (lines 121-160)
- [ ] T025 [US4] Add access policy configuration placeholder in `scripts/provision-keyvault.sh` (lines 161-200)
- [ ] T026 [US4] Add Key Vault URI output to file in `scripts/provision-keyvault.sh` (lines 201-220)
- [ ] T027 [US4] Test script: Generate self-signed cert and verify 365-day expiry
- [ ] T028 [US4] Test script: Provision Key Vault and verify secrets stored
- [ ] T029 [US4] Test script: Retrieve secret from Key Vault and validate content

**Checkpoint**: Key Vault functional with test certificates - ready for container app integration

---

## Phase 5: User Story 1 - Deploy Test Environment (Priority: P0)

**Goal**: Complete Azure Container Apps test environment with server deployed and healthy

**Independent Test**:
```bash
./scripts/deploy-test-env.sh
curl https://$(cat .server-url)/health
# Expected: HTTP 200 {"status":"healthy","version":"1.0.0"}
```

### Implementation for User Story 1

- [ ] T030 [P] [US1] Create Container Apps environment provisioning in `scripts/deploy-test-env.sh` (lines 1-80)
- [ ] T031 [P] [US1] Add Key Vault URI validation and loading in `scripts/deploy-test-env.sh` (lines 81-110)
- [ ] T032 [US1] Implement server container app deployment in `scripts/deploy-test-env.sh` (lines 111-180)
- [ ] T033 [US1] Add managed identity assignment in `scripts/deploy-test-env.sh` (lines 181-200)
- [ ] T034 [US1] Configure health probe on port 8080 in `scripts/deploy-test-env.sh` (lines 201-230)
- [ ] T035 [US1] Add Key Vault access policy grant in `scripts/deploy-test-env.sh` (lines 231-260)
- [ ] T036 [US1] Implement health check polling logic in `scripts/deploy-test-env.sh` (lines 261-300)
- [ ] T037 [US1] Add server URL output and next steps in `scripts/deploy-test-env.sh` (lines 301-340)
- [ ] T038 [US1] Test deployment: Full test environment provisioning end-to-end
- [ ] T039 [US1] Test deployment: Verify health endpoint accessible and returns 200
- [ ] T040 [US1] Test deployment: Verify managed identity has Key Vault access

**Checkpoint**: Test environment deployed and server healthy - ready for e2e testing

---

## Phase 6: User Story 3 - E2E Test Tunnel Flow (Priority: P0)

**Goal**: Automated end-to-end validation of complete tunnel flow

**Independent Test**:
```bash
./scripts/e2e-test.sh --env test
# Expected: All tests passed (7/7)
```

### Implementation for User Story 3

- [ ] T041 [P] [US3] Create test service HTTP echo server in `test/e2e/test-service.py` (complete file ~100 lines)
- [ ] T042 [P] [US3] Create test helper utilities in `test/e2e/helpers.sh` (logging, assertions, cleanup)
- [ ] T043 [P] [US3] Initialize test framework in `scripts/e2e-test.sh` (lines 1-60, setup, config)
- [ ] T044 [US3] Implement tunnel creation test in `scripts/e2e-test.sh` (lines 61-100, Test 1)
- [ ] T045 [US3] Implement test service startup in `scripts/e2e-test.sh` (lines 101-130, Test 2)
- [ ] T046 [US3] Implement agent startup and connection test in `scripts/e2e-test.sh` (lines 131-180, Test 3)
- [ ] T047 [US3] Implement proxied request test in `scripts/e2e-test.sh` (lines 181-220, Test 4)
- [ ] T048 [US3] Implement invalid token rejection test in `scripts/e2e-test.sh` (lines 221-260, Test 5)
- [ ] T049 [US3] Implement agent disconnect 503 test in `scripts/e2e-test.sh` (lines 261-290, Test 6)
- [ ] T050 [US3] Implement cleanup test in `scripts/e2e-test.sh` (lines 291-320, Test 7)
- [ ] T051 [US3] Add test results aggregation in `scripts/e2e-test.sh` (lines 321-370, success/fail recording)
- [ ] T052 [US3] Add JSON output formatting in `scripts/e2e-test.sh` (lines 371-420)
- [ ] T053 [US3] Add verbose logging mode in `scripts/e2e-test.sh` (lines 421-450)
- [ ] T054 [US3] Test e2e: Run full test suite and verify all 7 scenarios pass
- [ ] T055 [US3] Test e2e: Verify test execution completes in <2 minutes
- [ ] T056 [US3] Test e2e: Verify cleanup removes all resources

**Checkpoint**: E2E tests fully automated and passing - deployment validated

---

## Phase 7: User Story 5 - DNS Configuration (Priority: P1)

**Goal**: Azure DNS configured for tunnel subdomains with wildcard records

**Independent Test**:
```bash
./scripts/configure-dns.sh --env test --zone test.tunnel.example.com
dig @8.8.8.8 test-api.test.tunnel.example.com
# Expected: A record resolving to container app IP
```

### Implementation for User Story 5

- [ ] T057 [P] [US5] Create DNS zone provisioning in `scripts/configure-dns.sh` (lines 1-80)
- [ ] T058 [P] [US5] Add wildcard A/CNAME record creation in `scripts/configure-dns.sh` (lines 81-130)
- [ ] T059 [US5] Add root A record for Management API in `scripts/configure-dns.sh` (lines 131-160)
- [ ] T060 [US5] Implement DNS resolution validation in `scripts/configure-dns.sh` (lines 161-200)
- [ ] T061 [US5] Add custom domain configuration on container app in `scripts/configure-dns.sh` (lines 201-250)
- [ ] T062 [US5] Add TLS certificate binding for custom domain in `scripts/configure-dns.sh` (lines 251-300)
- [ ] T063 [US5] Test DNS: Create DNS zone and verify propagation within 5 minutes
- [ ] T064 [US5] Test DNS: Verify wildcard record resolves correctly
- [ ] T065 [US5] Test DNS: Verify HTTPS request to custom domain succeeds

**Checkpoint**: DNS operational with custom domains - production-ready routing

---

## Phase 8: User Story 6 - Monitoring and Logging (Priority: P2)

**Goal**: Application Insights and Log Analytics configured for observability

**Independent Test**:
```bash
./scripts/setup-monitoring.sh --env test
az monitor app-insights query --app protogate-test-insights \
  --analytics-query "traces | where message contains 'tunnel' | top 10 by timestamp"
# Expected: Recent log entries displayed
```

### Implementation for User Story 6

- [ ] T066 [P] [US6] Create Application Insights provisioning in `scripts/setup-monitoring.sh` (lines 1-70)
- [ ] T067 [P] [US6] Create Log Analytics workspace in `scripts/setup-monitoring.sh` (lines 71-120)
- [ ] T068 [P] [US6] Create alert rule definitions in `azure/alert-rules.json` (complete file)
- [ ] T069 [US6] Link Container Apps to Log Analytics in `scripts/setup-monitoring.sh` (lines 121-160)
- [ ] T070 [US6] Configure Application Insights connection string in `scripts/setup-monitoring.sh` (lines 161-200)
- [ ] T071 [US6] Create alert rules in `scripts/setup-monitoring.sh` (lines 201-270, error rate, availability, latency)
- [ ] T072 [US6] Create metrics dashboard in `scripts/setup-monitoring.sh` (lines 271-320)
- [ ] T073 [US6] Test monitoring: Verify logs appear in Log Analytics within 1 minute
- [ ] T074 [US6] Test monitoring: Verify metrics queryable in Application Insights
- [ ] T075 [US6] Test monitoring: Verify alert rules active

**Checkpoint**: Full observability stack operational - production monitoring ready

---

## Phase 9: Production Deployment (Priority: P1)

**Goal**: Production environment deployed with HA configuration

**Independent Test**:
```bash
./scripts/deploy-prod-env.sh
./scripts/e2e-test.sh --env prod
# Expected: Production environment healthy, all tests pass
```

### Implementation for Production

- [ ] T076 [P] [PROD] Create production deployment script in `scripts/deploy-prod-env.sh` (adapt from test script)
- [ ] T077 [P] [PROD] Create cleanup script in `scripts/cleanup-test-env.sh` (resource deletion)
- [ ] T078 [PROD] Configure production settings: min 2 replicas, increased resources (1 CPU, 2Gi)
- [ ] T079 [PROD] Provision production Key Vault with CA-signed certificates
- [ ] T080 [PROD] Deploy production DNS zone with production domain
- [ ] T081 [PROD] Enable monitoring and alerts for production
- [ ] T082 [PROD] Run smoke tests against production environment
- [ ] T083 [PROD] Document production URLs and access in quickstart.md

**Checkpoint**: Production environment live and validated

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, refinement, and cross-story improvements

- [ ] T084 [P] Update main README.md with Azure deployment section
- [ ] T085 [P] Create deployment troubleshooting guide in `docs/troubleshooting-azure.md`
- [ ] T086 [P] Add CI/CD workflow template in `.github/workflows/azure-deploy.yml.template`
- [ ] T087 [P] Document cost optimization tips in quickstart.md
- [ ] T088 Validate all scripts with shellcheck and fix warnings
- [ ] T089 Add script unit tests for argument parsing and error handling
- [ ] T090 Run complete quickstart.md validation end-to-end
- [ ] T091 Create rollback procedure documentation
- [ ] T092 Document production checklist in quickstart.md

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Story 2 (Phase 3)**: Depends on Foundational - Builds Docker images (BLOCKS Phase 5)
- **User Story 4 (Phase 4)**: Depends on Foundational - Can run in parallel with Phase 3
- **User Story 1 (Phase 5)**: Depends on Phase 3 (images) and Phase 4 (Key Vault)
- **User Story 3 (Phase 6)**: Depends on Phase 5 (test environment deployed)
- **User Story 5 (Phase 7)**: Depends on Phase 5 - Can run in parallel with Phase 6
- **User Story 6 (Phase 8)**: Depends on Phase 5 - Can run in parallel with Phase 6-7
- **Production (Phase 9)**: Depends on Phase 6 (e2e tests passing)
- **Polish (Phase 10)**: Depends on all desired features complete

### User Story Dependencies

```
Foundational (Phase 2) ─┬─→ US2: Docker Images (Phase 3) ───┐
                        │                                     │
                        └─→ US4: Key Vault (Phase 4) ────────┼─→ US1: Deploy (Phase 5) ─→ US3: E2E Tests (Phase 6) ─→ Production (Phase 9)
                                                              │                              ↑
                                                              ├─→ US5: DNS (Phase 7) ───────┘
                                                              │
                                                              └─→ US6: Monitoring (Phase 8) ─┘
```

### Critical Path (P0 Tasks Only)

1. Phase 1: Setup (30 min)
2. Phase 2: Foundational (30 min)
3. Phase 3: Docker Images US2 (2 hours)
4. Phase 4: Key Vault US4 (1 hour) - **Can parallel with Phase 3**
5. Phase 5: Deploy Test Env US1 (3 hours)
6. Phase 6: E2E Tests US3 (3 hours)

**Total Critical Path**: 9.5 hours (with parallelization: 8.5 hours)

### Parallel Opportunities

**Within Foundational Phase**:
- T005, T006, T007 can run in parallel (different checks)
- T008, T009 can run in parallel with checks

**Phase 3 + Phase 4 Parallel**:
- US2 (Docker) and US4 (Key Vault) have no dependencies - full parallelization possible
- 2 developers can complete both in 2 hours instead of 3 hours sequentially

**Phase 6 + Phase 7 + Phase 8 Parallel**:
- US3 (E2E), US5 (DNS), US6 (Monitoring) can all run in parallel after US1 complete
- 3 developers can complete all in 3 hours instead of 7 hours sequentially

**Within Each User Story**:
- Tasks marked [P] can run in parallel (different files)
- Example US2: T010, T011 can run simultaneously

---

## Parallel Example: User Story 2 (Docker Images)

```bash
# Launch model/setup tasks together:
Task T010: "Create buildx builder setup function in scripts/build-and-push.sh"
Task T011: "Add Docker prerequisite validation in scripts/build-and-push.sh"
# Both write to different line ranges in same file - coordinate or use branches

# After T010-T011 complete, continue:
Task T012: "Implement multi-arch build logic" (depends on T010, T011)
```

---

## Implementation Strategy

### MVP First (P0 Only)

1. **Week 1**: Complete Setup + Foundational → Foundation ready
2. **Week 1**: Complete US2 (Docker) + US4 (Key Vault) in parallel
3. **Week 2**: Complete US1 (Deploy Test Env)
4. **Week 2**: Complete US3 (E2E Tests)
5. **STOP and VALIDATE**: Test environment fully functional
6. Deploy, demo, gather feedback

**Estimated MVP Time**: 8.5 hours (1-2 days with parallelization)

### Incremental Delivery

1. **Sprint 1** (P0): Setup → Foundational → Docker → Key Vault → Deploy → E2E Tests
   - **Deliverable**: Test environment fully operational, automated e2e tests passing
   
2. **Sprint 2** (P1): DNS Configuration + Production Deployment
   - **Deliverable**: Custom domains working, production environment live
   
3. **Sprint 3** (P2): Monitoring + Polish
   - **Deliverable**: Full observability, documentation complete

### Parallel Team Strategy

With 3 developers after Foundational complete:

- **Developer A**: US2 (Docker Images) → US1 (Deploy) → US3 (E2E Tests) [Critical path]
- **Developer B**: US4 (Key Vault) → US5 (DNS) → US6 (Monitoring)
- **Developer C**: Documentation → Production scripts → Polish

**Timeline**: 2-3 days to MVP with parallel work

---

## Testing Validation

### Per User Story

- **US2**: Images build, push to ACR, both architectures <100MB
- **US4**: Key Vault provisioned, certs stored, retrieval works
- **US1**: Test environment deployed, health endpoint 200 OK
- **US3**: All 7 e2e test scenarios pass, execution <2 minutes
- **US5**: DNS resolves, custom domain works with HTTPS
- **US6**: Logs in Log Analytics, metrics in App Insights, alerts active

### Full System Test

After all P0 tasks complete:
```bash
# Build and push
./scripts/build-and-push.sh v1.0.0

# Provision Key Vault
./scripts/provision-keyvault.sh --env test

# Deploy test environment
./scripts/deploy-test-env.sh

# Run e2e tests
./scripts/e2e-test.sh --env test

# Expected: All green, no errors
```

---

## Notes

- **[P] tasks**: Different files or non-conflicting line ranges - safe to parallelize
- **[Story] labels**: Map tasks to user stories for independent delivery
- **File paths**: All paths from repository root (`/Volumes/Projects/protogate/`)
- **Script length estimates**: Provided in plan.md for each script
- **Tests optional**: This feature focuses on deployment scripts (testing the scripts themselves)
- **Idempotency**: All scripts should be safe to run multiple times
- **Error handling**: All scripts must validate prerequisites and provide clear error messages

---

## Total Task Count

- **Phase 1 (Setup)**: 4 tasks
- **Phase 2 (Foundational)**: 5 tasks
- **Phase 3 (US2 - Docker)**: 10 tasks
- **Phase 4 (US4 - Key Vault)**: 10 tasks
- **Phase 5 (US1 - Deploy)**: 11 tasks
- **Phase 6 (US3 - E2E Tests)**: 16 tasks
- **Phase 7 (US5 - DNS)**: 9 tasks
- **Phase 8 (US6 - Monitoring)**: 10 tasks
- **Phase 9 (Production)**: 8 tasks
- **Phase 10 (Polish)**: 9 tasks

**Total**: 92 tasks

**P0 Critical Path**: 45 tasks (Phases 1-6)  
**P1 Tasks**: 17 tasks (DNS + Production)  
**P2 Tasks**: 10 tasks (Monitoring)  
**Polish**: 9 tasks

**Suggested MVP Scope**: Phases 1-6 (45 tasks, ~8.5 hours)