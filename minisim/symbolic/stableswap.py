import marimo

__generated_with = "0.15.2"
app = marimo.App(width="medium")


@app.cell
def _():
    import marimo as mo
    import importlib
    from graphlib import TopologicalSorter
    from sympy import solve,sqrt,log,exp,oo,cosh,sinh,cbrt,Rational,Eq,Symbol,symbols,Expr
    from sympy.codegen.ast import Assignment,Declaration
    from sympy.codegen.rewriting import create_expand_pow_optimization
    import sympy
    import numpy as np
    import matplotlib.pyplot as plt
    return (
        Assignment,
        Declaration,
        Eq,
        Expr,
        Symbol,
        TopologicalSorter,
        cbrt,
        create_expand_pow_optimization,
        mo,
        solve,
        sqrt,
        symbols,
        sympy,
    )


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
    return A, D, Q, S, p, p_xy, q, s, x, y


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
    This is bad solution since for large $y$ we get catastrophic cancellation of form:

    $$\sqrt{x^2 + b} - x$$

    standard trick is conjugate multiplication:

    $$
    \frac{(\sqrt{x^2 + b} - x)(\sqrt{x^2 + b} + x)}{\sqrt{x^2 + b} + x}
    = \frac{b}{\sqrt{x^2 + b} + x}
    $$
    """
    )
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(
        r"""
    # Solving $x$, $y$ for $D$ and price

    We know expression for price:

    $$P = \frac{\partial J/\partial y}{\partial J/\partial x}$$

    Unfortunately we can't solve following system symbolically

    $$
    \left\{\begin{aligned}
    J(x/D, y/D) &= 0 \\
    P(x,y) &= P_0
    \end{aligned}\right.
    $$

    so we will fall to solving equation numerically. We arbitrarily pick $x$ as independent variable:

    $$P(x, y(x)) = P_0 $$

    For using Newton's method we need to compute:

    $$
    \frac{dP}{dx}
    = \frac{\partial P}{\partial x} + \frac{\partial P}{\partial y}\frac{dy}{dx}
    = \frac{\partial P}{\partial x} - \frac{1}{P}\frac{\partial P}{\partial y}
    $$
    """
    )
    return


@app.cell(hide_code=True)
def _(mo):
    mo.md(r"""Expression for price is simpler for `eq_D0`""")
    return


@app.cell
def _(eq_D0, x, y):
    price = (eq_D0.diff(y) / eq_D0.diff(x)).simplify()
    price
    return (price,)


@app.cell
def _(price, x, y):
    dP_dx = price.diff(x) - 1/price * price.diff(y)
    dP_dx = dP_dx.simplify().factor()
    dP_dx
    return (dP_dx,)


@app.cell
def _(Expr, Symbol, sympy):
    def cse_dict(dct: dict[Symbol, Expr]) -> dict[Symbol, Expr]:
        """
        Perform CSE on RHS of dictionary and return new dictionary
        """
        new,rhs = sympy.cse(list(dct.values()))
        r = {k:v for k,v in zip(dct, rhs)}
        for k,v in new:
            r[k] = v
        return r
    return (cse_dict,)


@app.cell
def _(
    Assignment,
    Expr,
    Symbol,
    TopologicalSorter,
    create_expand_pow_optimization,
    sympy,
):
    def codegen_c(dct:      dict[Symbol,Expr], 
                  declared: list[Symbol]|None = None, 
                  indent:   int = 0
                 ) -> str:
        dct = {sym: sympy.sympify(expr) for sym, expr in dct.items()}    
        # Sort expressions in dependency order. Cycles throw erros
        deps: dict[Symbol, set[Symbol]] = {
            sym: {s for s in expr.free_symbols if s in dct}
            for sym, expr in dct.items()
        }
        ordered    = TopologicalSorter(deps).static_order()
        expand_opt = create_expand_pow_optimization(4)
        code = [
            ' '*indent + row
            for sym in ordered
            for row in [
                ('' if declared is not None and sym in declared else 'const money ') + 
                sympy.ccode(Assignment(sym, expand_opt(dct[sym])))
            ]
        ]
        return '\n'.join(code)
    return (codegen_c,)


@app.cell
def _(symbols):
    D3 = symbols('D3')
    return (D3,)


@app.cell
def _(D, D3, dP_dx):
    dP_dx.subs(D**3, D3)
    return


@app.cell
def _(D, D3, Symbol, cse_dict, dP_dx):
    dpdx_cse = cse_dict({
        Symbol('dP_dx'): dP_dx.subs(D**3, D3),
        D3: D**3,
    })
    return (dpdx_cse,)


@app.cell
def _(dpdx_cse):
    dpdx_cse
    return


@app.cell
def _(Symbol, codegen_c, dpdx_cse):
    print(codegen_c(dpdx_cse,
                    declared = [Symbol('dP_dx')],
                    indent   = 12,
                   ))
    return


@app.cell
def _(Declaration):
    ttt = Declaration('z')
    #ttt.type = 'int'
    ttt.args[0].type = 'int'
    ttt
    return


@app.cell
def _(Symbol):
    [Symbol('dP_dx')]
    return


@app.cell
def _():
    return


if __name__ == "__main__":
    app.run()
