// an anonymous default function (named "default", as in JavaScript), generic
export default function <T>(xs: T[], fallback: T): T {
  return xs.length > 0 ? xs[xs.length - 1] : fallback;
}
export const tag = "default_fn";
