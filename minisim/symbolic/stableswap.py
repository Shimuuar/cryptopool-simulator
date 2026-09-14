import marimo

__generated_with = "0.15.2"
app = marimo.App(width="medium")


@app.cell
def _():
    import marimo as mo
    import importlib
    from sympy import solve,sqrt,log,exp,oo,cosh,sinh,cbrt,Rational,Eq,Symbol,symbols
    import sympy
    import numpy as np
    import matplotlib.pyplot as plt
    return Eq, cbrt, mo, solve, sqrt, symbols


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    # Deriving formulae for stableswap

    Note that implementation in minisim uses different implementation of scale for A than in original paper
    """
    )
    return


@app.cell
def _(symbols):
    A,B,D,P,x,x0,y,y0,s,Q = symbols('A B D P x x0 y y0 s Q', real=True, positive=True)
    p,q = symbols('p q', real=True)
    S,p_xy = symbols('S p_{xy}', real=True, positive=True)
    return A, D, P, Q, S, p, p_xy, q, s, x, y, y0


@app.cell
def _(A, D, Eq, x, y):
    curve_paper_eq = Eq(2*A*(x+y) + D, 2*A*D + D**3/(4*x*y))
    curve_paper_eq
    return (curve_paper_eq,)


@app.cell
def _(curve_paper_eq):
    curve_paper = curve_paper_eq.rhs - curve_paper_eq.lhs
    curve_paper
    return (curve_paper,)


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""Find set of values for $D$ which are used to check that we're implementing correct curve:""")
    return


@app.cell
def _(A, curve_paper, solve, x, y):
    def solve_for_D_numeric(_A,_x,_y):
        eq = curve_paper.subs({A:_A, x:_x, y:_y})
        return [
            rr
            for r  in solve(eq)
            for rr in (r.evalf(),)
            if rr.is_real
        ]
    return (solve_for_D_numeric,)


@app.cell
def _(solve_for_D_numeric):
    print('\n'.join([
        str((x,y,solve_for_D_numeric(5,x,y)))
        for x,y in [(1,1), (1,2), (1,3), (1,4), (1,5)]
    ]))
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    ## Solving for D

    First we obtain analytic solution which is possible for cubic equation
    """
    )
    return


@app.cell
def _(D, curve_paper):
    eq_D0 = (curve_paper).expand().collect(D)
    eq_D0
    return (eq_D0,)


@app.cell
def _(D, eq_D0, x, y):
    eq_D = (eq_D0 * 4*x*y).expand().collect(D)
    eq_D
    return (eq_D,)


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    Note this is very conveninently depressed cubic equation:

    $$x^3 + px + q = 0$$
    """
    )
    return


@app.cell
def _(D, Eq, eq_D, p, q):
    assert eq_D.coeff(D,3) == 1
    assert eq_D.coeff(D,2) == 0
    p_D = eq_D.coeff(D, 1).factor()
    q_D = eq_D.coeff(D, 0).factor()
    [Eq(p, p_D), Eq(q,q_D)]
    return p_D, q_D


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""Discriminant of equation is:""")
    return


@app.cell
def _(p_D, q_D, x, y):
    D_discr = (-(27*q_D**2 + 4*p_D**3) / (64 * x**2 * y**2)).simplify()
    D_discr
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""It's clearly negative for $A>1/2$. In this case we have single real root. Case $A<1/2$ is more complicated but we aren't really intereseted in it.""")
    return


@app.cell
def _(S, p_xy, x, y):
    dict_cse_D = {
        S:    x+y,
        p_xy: x*y,
    }
    return


@app.function
def rsub(e, dct):
    return e.subs({v:k for k,v in dct.items()})


@app.cell
def _(p_D, q_D, sqrt, x, y):
    _sqrt_D = sqrt(q_D**2 / 4 + p_D**3 / 27)
    sqrt_D  = sqrt((_sqrt_D.args[0]/(4*x*y)**2).simplify()) * 4*x*y
    assert (sqrt_D - _sqrt_D).simplify() == 0
    sqrt_D
    return (sqrt_D,)


@app.cell
def _(cbrt, q_D, sqrt_D):
    root_D = -cbrt(q_D/2 + sqrt_D) + cbrt(-q_D/2 + sqrt_D)
    root_D
    return (root_D,)


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    Now how does root looks in $s$,$Q$ variables:

    $$
    \begin{aligned}
    s &= \sqrt{xy}\\
    Q &= \sqrt{\frac{x}{y}} + \sqrt{\frac{y}{x}}
    \end{aligned}
    $$
    """
    )
    return


@app.cell
def _(Q, root_D, s, x, y):
    root_D_s = root_D.subs({x*y:s**2, x+y: s*Q}).simplify()
    root_D_s
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    # Solving for $x$ given $y$, $D$

    Note that we know that $x \propto D$ so we can make substitution: 

    $$ x = \bar{x}D
    \qquad
    y = \bar{y}D
    $$
    """
    )
    return


@app.cell
def _(D, eq_D, x, y):
    eq_x = (
        eq_D.subs({x:x*D, y:y*D}) / D**3 / (-8*y)
    ).expand().collect(x)
    eq_x
    return (eq_x,)


@app.cell
def _(eq_x, sqrt, x):
    _A = eq_x.coeff(x,2)
    _B = eq_x.coeff(x,1)
    _C = eq_x.coeff(x,0)
    root_x = (-_B + sqrt(_B**2 - 4*_A*_C))/(2*_A)
    root_x
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    # Solving $x$, $y$ for $D$ and price

    In similar way we can make substitution and eliminate trivial dependency on $D$:

    $$ x = \bar{x}D
    \qquad
    y = \bar{y}D
    $$
    """
    )
    return


@app.cell
def _(D, eq_D, x, y):
    price = (eq_D.diff(y) / eq_D.diff(x)).subs({x:x*D, y:y*D}).simplify()
    price
    return (price,)


@app.cell
def _(D, eq_D, x, y):
    eq_x_P = (
        eq_D.subs({x:x*D, y:y*D}) / (-D**3)
    ).expand()
    eq_x_P
    return (eq_x_P,)


@app.cell
def _(P, price):
    _num, _denom = price.as_numer_denom()
    eq_price = (_num - P*_denom).expand()
    eq_price
    return (eq_price,)


@app.cell
def _(Q, eq_x_P, s, x, y):
    (eq_x_P.subs({x: s/Q, y:s*Q}) * Q).expand().collect(Q)
    return


@app.cell
def _(Q, eq_price, s, x, y):
    (eq_price.subs({x: s/Q, y:s*Q}) * Q**2).expand()
    return


@app.cell
def _(mo):
    mo.md(r"""Let take slighlty roundabout way""")
    return


@app.cell
def _(eq_x_P):
    eq_x_P
    return


@app.cell
def _(P, eq_x_P, x, y, y0):
    eq_Y = eq_x_P.subs(x, (y0-y)*P).expand().collect(y)
    eq_Y
    return (eq_Y,)


@app.cell
def _(eq_Y, y):
    eq_Y.coeff(y, 3).factor()
    return


@app.cell
def _(eq_Y, y, y0):
    def _discr():
        a = eq_Y.coeff(y, 3)
        b = eq_Y.coeff(y, 2)
        c = eq_Y.coeff(y, 1)
        d = eq_Y.coeff(y, 0)
        return 18*a*b*c*d - 4*b**3 * d + b**2 * c**2 - 4*a*c**3 - 27*a**2*d**2
    _discr().expand().collect(y0)
    return


@app.cell
def _():
    return


if __name__ == "__main__":
    app.run()
