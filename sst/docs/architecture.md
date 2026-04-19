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
  - Calculates astronomical data using a library (e.g., `suncalc` or similar).
  - Returns formatted JSON.

### 3. Data Storage
- **Phase 1 (Current)**: Hardcoded mapping within the Lambda or a shared constant file.
- **Phase 2 (Future)**: **Amazon DynamoDB** table to store configuration profiles indexed by `configId`.

## Technical Stack
- **Infrastructure as Code**: SST (v3/Ion or v2)
- **Runtime**: Node.js / TypeScript
- **Cloud Provider**: AWS
- **Key Services**: Lambda, API Gateway, (Future) DynamoDB.

## Data Flow
1. Client calls `GET /astro/krakow-home`.
2. API Gateway triggers the Lambda.
3. Lambda looks up `krakow-home` to find coordinates (e.g., `50.0647° N, 19.9450° E`).
4. Lambda computes times for the current date.
5. Lambda returns:
   ```json
   {
     "configId": "krakow-home",
     "sun": { "rise": "05:42", "set": "19:51" },
     "moon": { "rise": "14:20", "set": "02:10" }
   }
   ```
