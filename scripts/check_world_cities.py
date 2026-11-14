"""Opt-in release smoke test: run with the path to the built world_city_probe."""
import datetime
import subprocess
import sys
import time
import urllib.request
from zoneinfo import ZoneInfo

cases = [
    ("Seoul", "KR", "Asia/Seoul"), ("Vancouver", "CA", "America/Vancouver"),
    ("New York", "US", "America/New_York"), ("London", "GB", "Europe/London"),
    ("São Paulo", "BR", "America/Sao_Paulo"), ("Nairobi", "KE", "Africa/Nairobi"),
    ("Kathmandu", "NP", "Asia/Kathmandu"), ("Adelaide", "AU", "Australia/Adelaide"),
    ("Auckland", "NZ", "Pacific/Auckland"), ("Tokyo", "JP", "Asia/Tokyo"),
    ("Berlin", "DE", "Europe/Berlin"), ("Honolulu", "US", "Pacific/Honolulu"),
]

def probe(mode, a, b="-", payload=b""):
    return subprocess.run([sys.argv[1], mode, a, b], input=payload, capture_output=True, check=True).stdout.decode()

def fetch(url):
    with urllib.request.urlopen(url, timeout=15) as r:
        return r.read(100000)

if __name__ == "__main__":
    failed = []
    for name, country, expected in cases:
        try:
            geodata = fetch(probe("geo-url", name, country))
            zone, offset, forecast_url, air_url = probe("resolve", country, payload=geodata).splitlines()
            # Some providers return IANA aliases. Compare offsets in both seasons too.
            now = datetime.datetime.now(datetime.timezone.utc)
            for dt in (now, now.replace(month=1, day=15), now.replace(month=7, day=15)):
                if dt.astimezone(ZoneInfo(zone)).utcoffset() != dt.astimezone(ZoneInfo(expected)).utcoffset():
                    raise ValueError("wrong time zone: "+zone)
            if int(offset) != now.astimezone(ZoneInfo(expected)).utcoffset().total_seconds():
                raise ValueError("firmware UTC offset differs from reference")
            probe("forecast", zone, payload=fetch(forecast_url))
            if country != "CA":
                probe("air", zone, payload=fetch(air_url))
            print("PASS", name, country, zone, offset, flush=True)
        except Exception as e:
            print("FAIL", name, str(e), getattr(e,"stderr",b"").decode(), flush=True)
            failed.append(name)
        time.sleep(0.3)
    sys.exit(bool(failed))
