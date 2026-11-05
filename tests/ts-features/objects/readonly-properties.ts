// @feature: `readonly` properties
// @category: Object Types
// @status: supported
interface Home { readonly owner: string; readonly rooms: string[] }
const h: Home = { owner: "Ann", rooms: ["kitchen"] };
h.rooms.push("hall");
console.log(h.owner, h.rooms);
