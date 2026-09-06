# AstroWeather API Architecture

## Overview
AstroWeather is a serverless API built on **AWS** using the **SST (Serverless Stack)** framework. The primary goal is to provide sunrise, sunset, moonrise, and moonset times for a specific location identified by a configuration ID.

## Core Components
### 1. API Gateway (SST `Api`)
- **Endpoint**: `GET /astro/{configId}`
- **Input**: `configId` (path parameter)
- **Output**: JSON payload containing astronomical rise/set times.

### 2. Lambda Function
- **Handler**: Processes the `configId`.
- **Logic**: 
  - Retrieves location data (latitude, longitude, timezone).
  - Calculates astronomical data using the **`suncalc`** library.
  - Returns formatted JSON.

### 3. Data Storage
- **Phase 1 (Current)**: Hardcoded mapping within the Lambda or a shared constant file.
  ```typescript
  const locations = {
    "krakow-home": { lat: 50.0647, lon: 19.9450, tz: "Europe/Warsaw" }
  };
  ```
- **Phase 2 (Future)**: **Amazon DynamoDB** table to store configuration profiles indexed by `configId`.

## Technical Stack
- **Infrastructure as Code**: SST (v3/Ion or v2)
- **Runtime**: Node.js / TypeScript
- **Library**: `suncalc`
- **Cloud Provider**: AWS

## Project Structure (Proposed)
- `sst/stacks/`: Infrastructure definitions.
- `sst/packages/functions/src/`: Lambda handler code.
- `sst/docs/`: Documentation.

## Data Flow
1. Client calls `GET /astro/krakow-home`.
2. API Gateway triggers the Lambda.
3. Lambda looks up `krakow-home` coordinates.
4. Lambda computes times using `suncalc.getTimes()` and `suncalc.getMoonTimes()`.
5. Lambda returns the JSON response.

## Web UI

The React/TypeScript web UI lives in `packages/web` and is hosted by an SST
`StaticSite` component backed by S3 and CloudFront. At build time, SST injects
the API URL as `VITE_API_URL`. The browser calls API Gateway, which invokes the
Lambda and returns the sun and moon data for the selected location.

The project structure is:

```text
sst/
├── packages/functions/   # API Lambda handlers
├── packages/web/         # Vite + React UI
├── docs/                 # Architecture and testing documentation
└── sst.config.ts         # API and StaticSite infrastructure
```

## Implementation Steps
1. Initialize SST project in the `sst` folder.
2. Install `suncalc` and `@types/suncalc`.
3. Create a Lambda handler that parses `configId`, looks up location, and calls `suncalc`.
4. Set up the SST API stack.
