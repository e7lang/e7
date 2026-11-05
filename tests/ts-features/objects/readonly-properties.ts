// @feature: `readonly` properties
// @category: Object Types
// @status: supported
// @note: tsc rejects writes; E7 itself enforces `readonly` only on class fields (an assignment to a readonly interface property compiles)
interface Home { readonly owner: string; readonly rooms: string[] }
const h: Home = { owner: "Ann", rooms: ["kitchen"] };
h.rooms.push("hall");
console.log(h.owner, h.rooms);
