import "./globals.css";
import type { Metadata } from "next";
import type { ReactNode } from "react";

export const metadata: Metadata = {
  title: "Central Scrutinizer",
};

export default function RootLayout({
  children,
}: Readonly<{ children: ReactNode }>) {
  return (
    <html lang="en">
      <head>
        <meta
          content="default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self' ws: wss:; font-src 'self' data:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'"
          httpEquiv="Content-Security-Policy"
        />
      </head>
      <body>{children}</body>
    </html>
  );
}
