import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, test, vi } from "vitest";
import App from "./App";

const fetchMock = vi.fn();

const configurationsResponse = () => new Response(JSON.stringify([
  { id: "wroclaw", label: "Wrocław" },
  { id: "krakow", label: "Kraków" }
]), { status: 200 });

beforeEach(() => {
  vi.stubGlobal("fetch", fetchMock);
  fetchMock.mockReset();
});

describe("AstroWeather app", () => {
  test("renders backend configurations", async () => {
    fetchMock.mockResolvedValueOnce(configurationsResponse());
    render(<App />);
    expect(await screen.findByRole("option", { name: "Kraków" })).toBeInTheDocument();
    expect(await screen.findByRole("option", { name: "Wrocław" })).toBeInTheDocument();
  });

  test("submits the selected location and renders results", async () => {
    fetchMock
      .mockResolvedValueOnce(configurationsResponse())
      .mockResolvedValueOnce(new Response("protocol=2\nconfigurationId=krakow\n", {
        status: 200,
        headers: { "content-type": "text/plain; charset=utf-8" }
      }));

    render(<App />);
      await screen.findByRole("option", { name: "Kraków" });
    fireEvent.change(screen.getByLabelText("Location"), { target: { value: "krakow" } });
    fireEvent.click(screen.getByRole("button", { name: "Submit" }));

    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      expect.stringContaining("/astro/krakow")
    ));
    const responseTitle = await screen.findByText("API response");
    expect(responseTitle.parentElement?.querySelector("pre")?.textContent)
      .toBe("protocol=2\nconfigurationId=krakow\n");
    expect(screen.getByText(/HTTP 200/)).toBeInTheDocument();
  });

  test("defaults to Wrocław when submitted without changing the location", async () => {
    fetchMock
      .mockResolvedValueOnce(configurationsResponse())
      .mockResolvedValueOnce(new Response(JSON.stringify({
        configId: "wroclaw",
        timezone: "Europe/Warsaw",
        sun: { rise: "2026-09-06T04:00:00.000Z", set: "2026-09-06T17:00:00.000Z" },
        moon: { rise: null, set: null, alwaysUp: true, alwaysDown: false }
      }), { status: 200 }));

    render(<App />);
    await screen.findByRole("option", { name: "Wrocław" });
    expect(screen.getByLabelText("Location")).toHaveValue("wroclaw");
    fireEvent.click(screen.getByRole("button", { name: "Submit" }));

    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      expect.stringContaining("/astro/wroclaw")
    ));
  });

  test("renders API errors", async () => {
    fetchMock
      .mockResolvedValueOnce(configurationsResponse())
      .mockResolvedValueOnce(new Response("protocol=2\nerror=configuration_not_found\n", {
        status: 404,
        headers: { "content-type": "text/plain; charset=utf-8" }
      }));

    render(<App />);
    await screen.findByRole("option", { name: "Kraków" });
    fireEvent.change(screen.getByLabelText("Location"), { target: { value: "krakow" } });
    fireEvent.click(screen.getByRole("button", { name: "Submit" }));

    const responseTitle = await screen.findByText("API response");
    expect(responseTitle.parentElement?.querySelector("pre")?.textContent)
      .toBe("protocol=2\nerror=configuration_not_found\n");
  });

  test("opens Clearoutside and renders hourly risk indicators", async () => {
    fetchMock
      .mockResolvedValueOnce(configurationsResponse())
      .mockResolvedValueOnce(new Response(JSON.stringify({
      coordinates: { latitude: 50.0647, longitude: 19.945 },
      nights: [{
        nightId: "2026-09-11",
        hours: [
          { hour: 12, timestampUtc: "2026-09-11T11:00:00.000Z", temperatureC: 20, cloudCoverTotalPct: 10, precipitationProbabilityPct: 0, thunderstormRisk: true },
          { hour: 13, timestampUtc: "2026-09-11T12:00:00.000Z", temperatureC: 19, cloudCoverTotalPct: 20, precipitationProbabilityPct: 5, thunderstormRisk: false }
        ]
      }]
      }), { status: 200 }));

    render(<App />);
    fireEvent.click(screen.getByRole("tab", { name: "Clearoutside" }));
  await screen.findByRole("option", { name: "Kraków" });
    fireEvent.change(screen.getByLabelText("Configuration"), { target: { value: "krakow" } });
    fireEvent.click(screen.getByRole("button", { name: "Fetch forecast" }));

    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      expect.stringContaining("/tools/clearoutside"),
      expect.objectContaining({ method: "POST", body: JSON.stringify({ configurationId: "krakow" }) })
    ));
    expect(await screen.findByText("2026-09-11")).toBeInTheDocument();
    expect(screen.getByText("⚡")).toBeInTheDocument();
  });

  test("opens the NOAA 27-day tool with the first configuration preselected and renders a night from two consecutive UTC days", async () => {
    fetchMock
      .mockResolvedValueOnce(configurationsResponse())
      .mockResolvedValueOnce(new Response(JSON.stringify({
        configurationId: "wroclaw",
        coordinates: { latitude: 51.1079, longitude: 17.0385 },
        timezone: "Europe/Warsaw",
        source: {
          url: "https://services.swpc.noaa.gov/text/27-day-outlook.txt",
          fetchedAt: "2026-09-28T20:00:00.000Z",
          lastModified: null,
          issuedAt: "2026-09-28T02:21:00.000Z"
        },
        nights: [{
          nightId: "2026-10-04",
          spans: [
            { start: "2026-10-04T00:00:00.000Z", end: "2026-10-05T00:00:00.000Z", largestKp: 4, ap: 12, f107: 90 },
            { start: "2026-10-05T00:00:00.000Z", end: "2026-10-06T00:00:00.000Z", largestKp: 5, ap: 15, f107: 91 }
          ]
        }]
      }), { status: 200 }));

    render(<App />);
    fireEvent.click(screen.getByRole("tab", { name: "NOAA 27-day" }));
    await screen.findByRole("option", { name: "Wrocław" });
    expect(screen.getByLabelText("Configuration")).toHaveValue("wroclaw");
    fireEvent.click(screen.getByRole("button", { name: "Fetch forecast" }));

    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      expect.stringContaining("/tools/noaa-outlook"),
      expect.objectContaining({ method: "POST", body: JSON.stringify({ configurationId: "wroclaw" }) })
    ));
    expect(await screen.findByText("2026-10-04")).toBeInTheDocument();
    expect(screen.getByRole("columnheader", { name: "04 Oct 02:00–05 Oct 02:00" })).toBeInTheDocument();
    expect(screen.getByRole("columnheader", { name: "05 Oct 02:00–06 Oct 02:00" })).toBeInTheDocument();
    expect(screen.getByText("Largest Kp").parentElement?.textContent).toBe("Largest Kp45");
  });

  test("sends coordinates with a timezone to the OVATION tool", async () => {
    fetchMock
      .mockResolvedValueOnce(configurationsResponse())
      .mockResolvedValueOnce(new Response(JSON.stringify({
        coordinates: { latitude: 27.9, longitude: 34.3 },
        timezone: "Africa/Cairo",
        source: { url: "https://services.swpc.noaa.gov/json/ovation_aurora_latest.json", fetchedAt: "2026-09-28T20:00:00.000Z", lastModified: null },
        nights: [{
          nightId: "2026-09-28",
          spans: [{ start: "2026-09-28T20:03:00.000Z", end: "2026-09-28T21:27:00.000Z", probabilityPct: 7, cell: { latitude: 28, longitude: 34 } }]
        }]
      }), { status: 200 }));

    render(<App />);
    fireEvent.click(screen.getByRole("tab", { name: "OVATION" }));
    await screen.findByRole("option", { name: "Wrocław" });
    fireEvent.change(screen.getByLabelText("Input"), { target: { value: "coordinates" } });
    fireEvent.change(screen.getByLabelText("Latitude"), { target: { value: "27.9" } });
    fireEvent.change(screen.getByLabelText("Longitude"), { target: { value: "34.3" } });
    fireEvent.change(screen.getByLabelText("Timezone"), { target: { value: "Africa/Cairo" } });
    fireEvent.click(screen.getByRole("button", { name: "Fetch forecast" }));

    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      expect.stringContaining("/tools/ovation"),
      expect.objectContaining({ method: "POST", body: JSON.stringify({ latitude: 27.9, longitude: 34.3, timezone: "Africa/Cairo" }) })
    ));
    expect(await screen.findByRole("columnheader", { name: "23:03–00:27" })).toBeInTheDocument();
    expect(screen.getByText("7%")).toBeInTheDocument();
  });
});
