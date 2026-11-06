// @feature: `Intl` and `toLocaleString`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: JavaScript-identical behaviour: locale data differs between platforms
// @instead: format with `toFixed`, `padStart` and your own tables, or let the host format
// @error: 'Intl' is not supported
const fmt = new Intl.NumberFormat("en-US", { style: "currency", currency: "USD" });
console.log(fmt.format(1234.5), (1234.5).toLocaleString("de-DE"));
