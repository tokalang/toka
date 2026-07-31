#!/usr/bin/env python3
import audit_cross_module_nocapture as audit


audit.CONTRACT = "readonly"
audit.FLAG = "--experimental-memory-contracts=readonly"
audit.SCHEMA = "toka.cross-module-readonly-audit"
audit.REQUIRE_MACHINE_DELTA = False
audit.main()
