// @feature: Mapped types (including modifiers and key remapping)
// @category: Type Manipulation
// @status: unsupported
// @why: types known at compile time: E7 types are concrete; type-level computation is not implemented
// @instead: spell out the interface (with optional or readonly properties as needed)
// @error: index signatures are not supported
type OptionsFlags<T> = { [P in keyof T]: boolean };
interface Features { darkMode: () => void; newUserProfile: () => void }
const flags: OptionsFlags<Features> = { darkMode: true, newUserProfile: false };
console.log(flags);
