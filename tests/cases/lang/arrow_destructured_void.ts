// An arrow with a destructured parameter and an expression body where a void function is expected.
type Listener<T> = (payload: T) => Promise<void>;
type Sync<T> = (payload: T) => void;
interface Notice {
  id: string;
}
const fns: Listener<Notice>[] = [];
fns.push(async (n) => console.log(n.id));
fns.push(async ({ id }) => console.log(id));
const s: Sync<Notice> = ({ id }) => console.log(id);
const s2: Sync<Notice> = (n) => console.log(n.id);
s({ id: "s" });
s2({ id: "s2" });
fns[0]({ id: "a" }).then(() => fns[1]({ id: "b" }));
