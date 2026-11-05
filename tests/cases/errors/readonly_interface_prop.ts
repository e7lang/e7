// @error: cannot assign to 'owner' because it is a read-only property
interface Home { readonly owner: string; rooms: string[] }
const h: Home = { owner: "Ann", rooms: [] };
h.rooms.push("hall");  // the array itself is mutable
h.owner = "Bob";
console.log(h.owner);
