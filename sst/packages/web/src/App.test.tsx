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
      .mockResolvedValueOnce(new Response("protocol=1\nconfigurationId=krakow\n", {
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
      .toBe("protocol=1\nconfigurationId=krakow\n");
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
      .mockResolvedValueOnce(new Response("protocol=1\nerror=configuration_not_found\n", {
        status: 404,
        headers: { "content-type": "text/plain; charset=utf-8" }
      }));

    render(<App />);
    await screen.findByRole("option", { name: "Kraków" });
    fireEvent.change(screen.getByLabelText("Location"), { target: { value: "krakow" } });
    fireEvent.click(screen.getByRole("button", { name: "Submit" }));

    const responseTitle = await screen.findByText("API response");
    expect(responseTitle.parentElement?.querySelector("pre")?.textContent)
      .toBe("protocol=1\nerror=configuration_not_found\n");
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
});
