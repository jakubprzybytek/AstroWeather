// `aurora.kpMain` is the Kp at which an aurora becomes a naked-eye glow low in
// the north (level 2); levels 1 and 3 are one Kp below and above it. See
// docs/aurora-forecast-supplier.md#levels. To compute it for a new location:
//
// 1. Find the location's corrected geomagnetic (AACGM-v2) latitude, not the
//    centred-dipole one, which is about 3° too high over Europe. For example
//    with Python's `aacgmv2` package, at 110 km (the aurora's lower edge) and
//    the current date:
//      aacgmv2.convert_latlon(lat, lon, 110, datetime.now(), "G2A")[0]
// 2. kpMain = (66 − (mlat + 5)) / 2, rounded to the nearest half step: the
//    oval's equatorward edge sits at 66° at Kp 0 and moves 2° per Kp step, and
//    an aurora is seen about 5° of latitude beyond that edge, low on the
//    horizon.
// 3. Recompute every few years: the magnetic pole drifts.
export const configurations = {
  "wroclaw": {
    label: "Wrocław",
    location: {
      lat: 51.1079,
      lon: 17.0385,
      tz: "Europe/Warsaw"
    },
    aurora: {
      kpMain: 7
    }
  },
  "krakow": {
    label: "Kraków",
    location: {
      lat: 50.0647,
      lon: 19.945,
      tz: "Europe/Warsaw"
    },
    aurora: {
      kpMain: 7.5
    }
  }
} as const;
