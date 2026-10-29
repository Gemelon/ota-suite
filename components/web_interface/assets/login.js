// SPDX-FileCopyrightText: 2026 Thomas Stoll
// SPDX-License-Identifier: Apache-2.0

"use strict";

document.getElementById("login-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const button = document.getElementById("login-submit");
  if (button.disabled) return;
  button.disabled = true;
  const password = document.getElementById("login-password");
  const errorBox = document.getElementById("login-error");
  errorBox.classList.add("d-none");
  try {
    const body = JSON.stringify({
      username: document.getElementById("login-username").value,
      password: password.value,
    });
    password.value = "";
    const response = await fetch("/api/auth/login", {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json", "X-Web-Login": "1" },
      body,
      signal: AbortSignal.timeout(10000),
    });
    if (!response.ok) throw new Error(I18n.message(await response.text()));
    location.replace("/");
  } catch (error) {
    errorBox.textContent = error.message;
    errorBox.classList.remove("d-none");
    password.focus();
  } finally {
    button.disabled = false;
  }
});
