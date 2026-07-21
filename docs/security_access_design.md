# Security Access Design with CGW-SEC Integration

## Overview

This document describes the security access (0x27) implementation in CGW-DIAG 
that delegates cryptographic operations to CGW-SEC via SOME/IP proxy.

## Architecture

### Layering Principle
- **Protocol Layer (CGW-DIAG)**: Handles UDS protocol interaction, session state, 
  and failure lockout mechanism
- **Cryptographic Layer (CGW-SEC)**: Performs seed generation and key verification

### Communication Flow
1. Diagnostic Tester → CGW-DIAG: 0x27 SecurityAccess (Request Seed)
2. CGW-DIAG → CGW-SEC: getSeed(level) via SOME/IP proxy
3. CGW-SEC → CGW-DIAG: SecuritySeed (seed_id + random seed)
4. CGW-DIAG → Diagnostic Tester: Positive Response + Seed
5. Diagnostic Tester → CGW-DIAG: 0x27 SecurityAccess (Send Key)
6. CGW-DIAG → CGW-SEC: verifyKey(level, seed_id, key) via SOME/IP proxy
7. CGW-SEC → CGW-DIAG: SecResult (SUCCESS/INVALID_KEY)
8. CGW-DIAG → Diagnostic Tester: Positive/Negative Response

## Security Features

### Seed/Key Verification
- Seed generation: Cryptographically random, unique per session
- Key verification: Delegated to CGW-SEC via AES-128 (aligned with TBOX-SEC)
- Single security level: Level 1 (default)

### Failure Lockout
- Maximum failed attempts: 3
- Lockout duration: 10 seconds
- Automatic reset after lockout period

## Error Handling

### NRC Mapping
- 0x35 (invalidKey): Mapped to CGW-DIAG-1011 (INVALID_KEY)
- Service unavailable: Mapped to CGW-DIAG-1010 (VIN_WRITE_REJECTED)

## Configuration

### CGW-SEC Service Configuration
- Service ID: Configurable via SomeIpServiceConfig
- Instance ID: 0x0001 (default)
- Communication: SOME/IP over vehicle Ethernet

## Testing

### Unit Tests
- Mock CGW-SEC proxy for isolated testing
- Test success and failure scenarios
- Test lockout mechanism

### Integration Tests
- End-to-end VIN provisioning flow
- Verify CGW-SEC integration
