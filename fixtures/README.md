# Fixture provenance

These JSON files are curated, deterministic parser samples. They are **not
byte-for-byte archived live HTTP responses**. Values, dates, and identifiers
are fixed so tests are repeatable. In particular,
`eccc_alerts_vancouver.json` contains synthetic alerts with `fixture-` IDs;
it is not evidence that ECCC issued those alerts.

The samples exercise Open-Meteo forecast/geocoding/time-zone fields,
Nominatim lookup fields, and ECCC AQHI/community/alert fields. Additional
malformed, missing, duplicate, expired, and out-of-range payloads are built
directly in `host/tests/test_providers.cpp`.

Parser tests establish behavior for those supplied schemas. Live service
availability, current response schemas, observation coverage, and physical
network behavior require separate checks. Do not use fixture observations as
current weather, time, or safety information.
