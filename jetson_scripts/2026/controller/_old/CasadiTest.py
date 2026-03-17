import casadi as ca

x = ca.MX.sym("x")
f = (x - 1)**2  # simple objective

nlp = {"x": x, "f": f}

try:
    solver = ca.nlpsol("solver", "ipopt", nlp)
    print("✅ IPOPT is available and working.")
except Exception as e:
    print("❌ IPOPT is NOT available.")
    print("Error:", e)

