#!/usr/bin/env python3
"""Fetch the weather for the SKJÁR Weather widget (Widget strip of the touch UI).

Asks Open-Meteo (https://open-meteo.com/, no API key; free for non-commercial use) for the
current weather, today's and tomorrow's range, and the next few hours, and writes weather.txt
next to ui.conf. The UI reloads it when it changes (software/ui/src/widgets.c). On the device,
a systemd timer runs this every 30 minutes (software/os/systemd/skjar-weather.timer).

    python3 fetch_weather.py                        # Tokyo
    python3 fetch_weather.py --city Sapporo
    python3 fetch_weather.py --lat 35.68 --lon 139.77 --name Home

weather.txt (one name=value per line; temperatures in degrees C):
    place=Tokyo
    now=<temp>,<WMO code>
    today=<max>,<min>,<precipitation probability %>
    tomorrow=<max>,<min>,<precipitation probability %>,<WMO code>
    hour=<HH:MM>,<temp>,<WMO code>,<precipitation probability %>    (up to 4, every --step hours)

Only the Python standard library is used.
"""
import argparse
import json
import os
import sys
import urllib.parse
import urllib.request

USER_AGENT = "skjar-weather/1.0"
TIMEOUT = 20
GEOCODE = "https://geocoding-api.open-meteo.com/v1/search"
FORECAST = "https://api.open-meteo.com/v1/forecast"


def default_out():
    """The folder of ui.conf (software/ui/src/settings.c)."""
    if os.name == "nt":
        folder = os.path.join(os.environ.get("APPDATA", "."), "skjar")
    else:
        xdg = os.environ.get("XDG_CONFIG_HOME")
        folder = os.path.join(xdg, "skjar") if xdg else os.path.expanduser("~/.config/skjar")
    return os.path.join(folder, "weather.txt")


def get_json(url, params):
    req = urllib.request.Request(url + "?" + urllib.parse.urlencode(params), headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return json.load(r)


def ascii_name(name):
    # The UI draws the place name with an ASCII-only font
    return "".join(c for c in name if " " <= c <= "~").strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--city", default="Tokyo", help="place name to look up (default Tokyo)")
    ap.add_argument("--lat", type=float, help="latitude (with --lon, instead of --city)")
    ap.add_argument("--lon", type=float, help="longitude")
    ap.add_argument("--name", help="place name to show (default: from the lookup, or the city)")
    ap.add_argument("--step", type=int, default=3, help="hours between the forecast slots (default 3)")
    ap.add_argument("--out", default=default_out(), help="output file (default: weather.txt next to ui.conf)")
    args = ap.parse_args()

    try:
        if args.lat is not None and args.lon is not None:
            lat, lon, place = args.lat, args.lon, args.name or "Weather"
        else:
            found = get_json(GEOCODE, {"name": args.city, "count": 1, "language": "en", "format": "json"})
            if not found.get("results"):
                print(f"fetch_weather: no place called {args.city!r}", file=sys.stderr)
                return 1
            r = found["results"][0]
            lat, lon = r["latitude"], r["longitude"]
            place = args.name or ascii_name(r.get("name", "")) or args.city
        w = get_json(FORECAST, {
            "latitude": lat, "longitude": lon, "timezone": "auto", "forecast_days": 2,
            "current": "temperature_2m,weather_code",
            "hourly": "temperature_2m,weather_code,precipitation_probability",
            "daily": "temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code",
        })
    except Exception as e:  # network down, bad answer: keep the old weather.txt
        print(f"fetch_weather: {e}", file=sys.stderr)
        return 1

    cur, hourly, daily = w["current"], w["hourly"], w["daily"]
    lines = [
        f"place={ascii_name(place)}",
        f"now={cur['temperature_2m']},{cur['weather_code']}",
    ]
    pop = daily.get("precipitation_probability_max") or [0, 0]
    lines.append(f"today={daily['temperature_2m_max'][0]},{daily['temperature_2m_min'][0]},{pop[0] or 0}")
    if len(daily["time"]) > 1:
        lines.append(f"tomorrow={daily['temperature_2m_max'][1]},{daily['temperature_2m_min'][1]},"
                     f"{pop[1] or 0},{daily['weather_code'][1]}")

    # The next slots after the current hour ("2026-11-09T15:00" sorts as text)
    now_hour = cur["time"][:13] + ":00"
    times = hourly["time"]
    start = next((i for i, t in enumerate(times) if t > now_hour), len(times))
    for i in range(start + args.step - 1, len(times), args.step)[:4]:
        hp = hourly["precipitation_probability"][i]
        lines.append(f"hour={times[i][11:16]},{hourly['temperature_2m'][i]},{hourly['weather_code'][i]},{hp or 0}")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    tmp = args.out + ".tmp"
    with open(tmp, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, args.out)   # the UI never sees a half-written file
    print(f"fetch_weather: {lines[0][6:]} -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
