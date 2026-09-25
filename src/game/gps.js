// GPS routing: A* over the road network (city streets, highways, ramps, town roads).
export function route(net, fromX, fromZ, toX, toZ) {
  if (!net) return [[fromX, fromZ], [toX, toZ]];
  const r = net.route(fromX, fromZ, toX, toZ, { filter: (e) => e.type !== 'dirt' || true });
  return net.routePolyline(r, fromX, fromZ, toX, toZ);
}
