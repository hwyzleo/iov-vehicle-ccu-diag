# Security Access Design with CCU-SEC Integration

## Overview

This document describes the security access (0x27) implementation in CCU-DIAG 
that delegates cryptographic operations to CCU-SEC via SOME/IP proxy.

## Architecture

### Layering Principle
- **Protocol Layer (CCU-DIAG)**: Handles UDS protocol interaction, session state, 
  and failure lockout mechanism
- **Cryptographic Layer (CCU-SEC)**: Performs seed generation and key verification

### Communication Flow
1. Diagnostic Tester → CCU-DIAG: 0x27 SecurityAccess (Request Seed)
2. CCU-DIAG → CCU-SEC: getSeed(level) via SOME/IP proxy
3. CCU-SEC → CCU-DIAG: SecuritySeed (seed_id + random seed)
4. CCU-DIAG → Diagnostic Tester: Positive Response + Seed
5. Diagnostic Tester → CCU-DIAG: 0x27 SecurityAccess (Send Key)
6. CCU-DIAG → CCU-SEC: verifyKey(level, seed_id, key) via SOME/IP proxy
7. CCU-SEC → CCU-DIAG: SecResult (SUCCESS/INVALID_KEY)
8. CCU-DIAG → Diagnostic Tester: Positive/Negative Response

## Security Features

### Seed/Key Verification
- Seed generation: Cryptographically random, unique per session
- Key verification: Delegated to CCU-SEC via AES-128 (aligned with TBOX-SEC)
- Single security level: Level 1 (default)

### Failure Lockout
- Maximum failed attempts: 3
- Lockout duration: 10 seconds
- Automatic reset after lockout period

## Error Handling

### NRC Mapping
- 0x35 (invalidKey): Mapped to CCU-DIAG-1011 (INVALID_KEY)
- Service unavailable: Mapped to CCU-DIAG-1010 (VIN_WRITE_REJECTED)

## Configuration

### CCU-SEC Service Configuration
- Service ID: Configurable via SomeIpServiceConfig
- Instance ID: 0x0001 (default)
- Communication: SOME/IP over vehicle Ethernet

## Testing

### Unit Tests
- Mock CCU-SEC proxy for isolated testing
- Test success and failure scenarios
- Test lockout mechanism

### Integration Tests
- End-to-end VIN provisioning flow
- Verify CCU-SEC integration
