import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, test, vi } from "vitest";
import App from "./App";

const fetchMock = vi.fn();

beforeEach(() => {
  vi.stubGlobal("fetch", fetchMock);
  fetchMock.mockReset();
});

describe("AstroWeather app", () => {
  test("renders both locations", () => {
    render(<App />);
    expect(screen.getByRole("option", { name: "Kraków" })).toBeInTheDocument();
    expect(screen.getByRole("option", { name: "Sharm El Sheikh" })).toBeInTheDocument();
  });

  test("submits the selected location and renders results", async () => {
    fetchMock.mockResolvedValue(new Response(JSON.stringify({
      configId: "krakow-home",
      timezone: "Europe/Warsaw",
      sun: { rise: "2026-09-06T04:00:00.000Z", set: "2026-09-06T17:00:00.000Z" },
      moon: { rise: null, set: null, alwaysUp: true, alwaysDown: false }
    }), { status: 200 }));

    render(<App />);
    fireEvent.change(screen.getByRole("combobox"), { target: { value: "krakow-home" } });
    fireEvent.click(screen.getByRole("button", { name: "Submit" }));

    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      expect.stringContaining("/astro/krakow-home")
    ));
    expect(await screen.findByText("The moon is always above the horizon.")).toBeInTheDocument();
  });

  test("renders API errors", async () => {
    fetchMock.mockResolvedValue(new Response(JSON.stringify({ message: "Configuration not found" }), { status: 404 }));

    render(<App />);
    fireEvent.change(screen.getByRole("combobox"), { target: { value: "krakow-home" } });
    fireEvent.click(screen.getByRole("button", { name: "Submit" }));

    expect(await screen.findByText("Configuration not found")).toBeInTheDocument();
  });
});
