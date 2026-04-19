import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { PairScreen } from "./pair-screen";

describe("PairScreen", () => {
  afterEach(() => {
    cleanup();
  });

  it("enables pairing once four digits are entered", () => {
    render(<PairScreen onSubmit={vi.fn().mockResolvedValue(undefined)} />);

    const input = screen.getByLabelText("Pairing code");
    const button = screen.getByRole("button", { name: "Pair Browser" });

    expect(button).toHaveProperty("disabled", true);
    fireEvent.change(input, { target: { value: "7391" } });
    expect(button).toHaveProperty("disabled", false);
  });

  it("shows an informational message when pairing is unavailable", () => {
    render(
      <PairScreen
        message="Reopen the handheld app to pair."
        onSubmit={vi.fn().mockResolvedValue(undefined)}
        pairingAvailable={false}
      />,
    );

    expect(screen.getByText(/Background mode is active on the handheld/i)).toBeTruthy();
    expect(screen.queryByLabelText("Pairing code")).toBeNull();
    expect(screen.queryByRole("button", { name: "Pair Browser" })).toBeNull();
  });
});
