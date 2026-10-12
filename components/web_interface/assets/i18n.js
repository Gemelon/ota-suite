"use strict";
(() => {
  const builtins = window.EBCLanguages;
  const english = builtins.en.strings;
  let strings = english;
  let code = "en";
  let locale = "en-GB";
  const normalize = value => String(value).replace(/\s+/g, " ").trim();
  const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const aliases = new Map();
  const patterns = [];
  for (const key of Object.keys(english)) {
    for (const source of [english[key], builtins.de.strings[key]]) {
      if (typeof source !== "string") continue;
      const text = normalize(source);
      if (/\{p\d+\}/.test(text)) continue;
      const tokens = [...text.matchAll(/%(?:[-+0 #]*\d*(?:\.\d+)?[lzjh]*)([sduif])/g)];
      if (tokens.length) {
        let last = 0, pattern = "^";
        tokens.forEach(token => {
          pattern += escape(text.slice(last, token.index)) +
            (token[1] === "s" ? "(.+?)" : "([-+\\d.,]+)");
          last = token.index + token[0].length;
        });
        patterns.push({key, regex: new RegExp(pattern + escape(text.slice(last)) + "$")});
      } else aliases.set(text, key);
    }
  }
  const fragments = [...aliases.keys()].filter(text => text.length >= 12)
    .sort((a,b) => b.length-a.length);
  const fragmentPattern = new RegExp(fragments.map(escape).join("|"), "g");

  function t(key) {
    if (!Object.hasOwn(english, key)) {
      console.error("Unknown translation key:", key);
      return key;
    }
    return Object.hasOwn(strings, key) ? strings[key] : english[key];
  }
  function f(key, values = {}) {
    return t(key).replace(/\{(p\d+)\}/g, (match, parameter) =>
      Object.hasOwn(values, parameter) ? String(values[parameter]) : match);
  }
  // Only protocol messages are passed here; never names, notes, filenames or other user data.
  function message(value) {
    const source = String(value ?? "");
    const normalized = normalize(source);
    const direct = aliases.get(normalized);
    if (direct) return t(direct);
    for (const pattern of patterns) {
      const match = normalized.match(pattern.regex);
      if (match) {
        let index = 0;
        return t(pattern.key).replace(/%(?:[-+0 #]*\d*(?:\.\d+)?[lzjh]*)([sduif])/g, () => match[++index]);
      }
    }
    // Preserve diagnostic values in composed firmware messages.
    return source.replace(fragmentPattern, fragment => t(aliases.get(fragment)));
  }
  function apply(root = document) {
    for (const element of root.querySelectorAll("[data-i18n]")) element.textContent = t(element.dataset.i18n);
    for (const element of root.querySelectorAll("[data-i18n-attrs]")) {
      for (const binding of element.dataset.i18nAttrs.split(";")) {
        const [attribute,key] = binding.split(":");
        if (["placeholder","title","aria-label"].includes(attribute)) element.setAttribute(attribute,t(key));
      }
    }
  }
  function validate(file, expected) {
    if (!file || file.version !== 1 || file.code !== expected ||
        !/^[a-z][a-z0-9-]{0,23}$/.test(file.code) || file.code.endsWith("-") ||
        typeof file.name !== "string" || !file.name.trim() || new TextEncoder().encode(file.name).length > 80 ||
        !file.strings || Array.isArray(file.strings) || typeof file.strings !== "object")
      throw new Error(t("language.invalid"));
    const entries = Object.entries(file.strings);
    if (entries.length > 1024) throw new Error(t("language.invalid"));
    const result = Object.create(null);
    for (const [key,value] of entries) {
      if (typeof value !== "string" || new TextEncoder().encode(value).length > 4096 || key.length > 80)
        throw new Error(t("language.invalid"));
      if (!Object.hasOwn(english,key)) continue;
      const placeholders = text => {
        const named = [...text.matchAll(/\{p\d+\}/g)].map(match=>match[0]).sort().join("|");
        const ordered = [...text.matchAll(/%(?:[-+0 #]*\d*(?:\.\d+)?[lzjh]*)(?:[sduif])/g)]
          .map(match=>match[0]).join("|");
        return `${named}/${ordered}`;
      };
      if (placeholders(value) !== placeholders(english[key])) throw new Error(`${t("language.placeholders")} ${key}`);
      result[key] = value;
    }
    return result;
  }
  async function get(url) {
    const response = await fetch(url,{cache:"no-store",credentials:"same-origin",signal:AbortSignal.timeout(8000)});
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }
  function warn(error) {
    console.warn("UI language:", error);
    const element = document.getElementById("language-error");
    if (element) {
      element.textContent = `${t("language.error")} ${message(error.message || error)}`;
      element.classList.remove("d-none");
    }
  }
  async function init() {
    let requested = navigator.language?.toLowerCase().startsWith("de") ? "de" : "en";
    try { requested = localStorage.getItem("ebctester.language") || requested; }
    catch (error) { warn(error); }
    const available = new Map([["en",builtins.en],["de",builtins.de]]);
    let catalog = null;
    try {
      catalog = builtins.sdEnabled ? await get("/api/i18n/languages") : {languages:[],errors:[]};
      if (!Array.isArray(catalog.languages) || !Array.isArray(catalog.errors)) throw new Error(t("language.invalid"));
      for (const entry of catalog.languages) {
        if (!entry || typeof entry.name !== "string" || !/^[a-z][a-z0-9-]{0,23}$/.test(entry.code))
          throw new Error(t("language.invalid"));
        available.set(entry.code,{...entry,sd:true});
      }
      if (catalog.errors.length) warn(catalog.errors.join(" "));
    } catch (error) { warn(error); }
    try {
      const selected = available.get(requested);
      if (!selected) throw new Error(`${t("language.unavailable")} ${requested}`);
      if (selected.sd) strings = {...english,...validate(await get(`/api/i18n/language?code=${encodeURIComponent(requested)}`),requested)};
      else strings = {...english,...selected.strings};
      code = requested;
    } catch (error) { strings=english; code="en"; warn(error); }
    document.documentElement.lang = code;
    try { locale = Intl.getCanonicalLocales(code)[0]; }
    catch (error) { warn(error); locale="en-GB"; }
    apply();
    const selector = document.getElementById("language-select");
    if (selector) {
      selector.replaceChildren();
      for (const entry of available.values()) {
        const option = document.createElement("option");
        option.value=entry.code;
        option.textContent=entry.name+(entry.sd?" (SD)":"");
        selector.append(option);
      }
      selector.value=code;
      selector.disabled=false;
      selector.addEventListener("change", () => {
        if (!window.confirm(t("language.reload"))) { selector.value=code; return; }
        try {
          localStorage.setItem("ebctester.language",selector.value);
          location.reload();
        } catch (error) { selector.value=code; warn(error); }
      });
    }
  }
  window.I18n = {
    t,f,message,apply,validate,
    get locale() { return locale; },
    get code() { return code; },
  };
  apply();
  I18n.ready = init();
})();
