import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import { PairScreen } from "./pair-screen";

describe("PairScreen", () => {
  it("enables pairing once four digits are entered", () => {
    render(<PairScreen onSubmit={vi.fn().mockResolvedValue(undefined)} />);

    const input = screen.getByLabelText("Pairing code");
    const button = screen.getByRole("button", { name: "Pair Browser" });

    expect(button).toHaveProperty("disabled", true);
    fireEvent.change(input, { target: { value: "7391" } });
    expect(button).toHaveProperty("disabled", false);
  });
});
