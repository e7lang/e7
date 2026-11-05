// @feature: URI functions (`encodeURIComponent`, `decodeURI`, ...)
// @category: JavaScript: Standard Library
// @status: supported
const q = "a b&c=d/é";
console.log(encodeURIComponent(q), encodeURI("https://x.org/a b?q=é"), decodeURIComponent("%E2%82%AC%20"), decodeURI("%41%2F"));
