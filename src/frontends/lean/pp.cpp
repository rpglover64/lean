/*
Copyright (c) 2014 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "util/flet.h"
#include "kernel/replace_fn.h"
#include "kernel/free_vars.h"
#include "library/aliases.h"
#include "library/scoped_ext.h"
#include "library/coercion.h"
#include "frontends/lean/pp.h"
#include "frontends/lean/pp_options.h"
#include "frontends/lean/token_table.h"

namespace lean {
static format g_ellipsis_n_fmt= highlight(format("\u2026"));
static format g_ellipsis_fmt  = highlight(format("..."));
static format g_lambda_n_fmt  = highlight_keyword(format("\u03BB"));
static format g_lambda_fmt    = highlight_keyword(format("fun"));
static format g_forall_n_fmt  = highlight_keyword(format("\u2200"));
static format g_forall_fmt    = highlight_keyword(format("forall"));
static format g_pi_n_fmt      = highlight_keyword(format("Π"));
static format g_pi_fmt        = highlight_keyword(format("Pi"));
static format g_arrow_n_fmt   = highlight_keyword(format("\u2192"));
static format g_arrow_fmt     = highlight_keyword(format("->"));

name pretty_fn::mk_metavar_name(name const & m) {
    if (auto it = m_purify_meta_table.find(m))
        return *it;
    name new_m = m_meta_prefix.append_after(m_next_meta_idx);
    m_next_meta_idx++;
    m_purify_meta_table.insert(m, new_m);
    return new_m;
}

name pretty_fn::mk_local_name(name const & m) {
    unsigned i = 1;
    name r = m;
    while (m_purify_locals.contains(r)) {
        r = m.append_after(i);
        i++;
    }
    m_purify_locals.insert(r);
    return r;
}

level pretty_fn::purify(level const & l) {
    if (!m_universes || !has_meta(l))
        return l;
    return replace(l, [&](level const & l) {
            if (!has_meta(l))
                return some_level(l);
            if (is_meta(l))
                return some_level(mk_meta_univ(mk_metavar_name(meta_id(l))));
            return none_level();
        });
}

/** \brief Make sure that all metavariables have reasonable names,
    and for all local constants l1 l2, local_pp_name(l1) != local_pp_name(l2).

    \remark pretty_fn will create new local constants when pretty printing,
    but it will make sure that the new constants will not produce collisions.
*/
expr pretty_fn::purify(expr const & e) {
    if (!has_expr_metavar(e) && !has_local(e) && (!m_universes || !has_univ_metavar(e)))
        return e;
    return replace(e, [&](expr const & e, unsigned) {
            if (!has_expr_metavar(e) && !has_local(e) && (!m_universes || !has_univ_metavar(e)))
                return some_expr(e);
            else if (is_metavar(e))
                return some_expr(mk_metavar(mk_metavar_name(mlocal_name(e)), mlocal_type(e)));
            else if (is_local(e))
                return some_expr(mk_local(mlocal_name(e), mk_local_name(local_pp_name(e)), mlocal_type(e), local_info(e)));
            else if (is_constant(e))
                return some_expr(update_constant(e, map(const_levels(e), [&](level const & l) { return purify(l); })));
            else if (is_sort(e))
                return some_expr(update_sort(e, purify(sort_level(e))));
            else
                return none_expr();
        });
}

void pretty_fn::set_options(options const & o) {
    m_indent    = get_pp_indent(o);
    m_max_depth = get_pp_max_depth(o);
    m_max_steps = get_pp_max_steps(o);
    m_implict   = get_pp_implicit(o);
    m_unicode   = get_pp_unicode(o);
    m_coercion  = get_pp_coercion(o);
    m_notation  = get_pp_notation(o);
    m_universes = get_pp_universes(o);
}

format pretty_fn::pp_level(level const & l) {
    return ::lean::pp(l, m_unicode, m_indent);
}

bool pretty_fn::is_implicit(expr const & f) {
    if (m_implict)
        return false; // showing implicit arguments
    try {
        binder_info bi = binding_info(m_tc.ensure_pi(m_tc.infer(f)));
        return bi.is_implicit() || bi.is_strict_implicit();
    } catch (...) {
        return false;
    }
}

bool pretty_fn::is_prop(expr const & e) {
    try {
        return m_env.impredicative() && m_tc.is_prop(e);
    } catch (...) {
        return false;
    }
}

auto pretty_fn::pp_child(expr const & e, unsigned bp) -> result {
    if (is_app(e) && is_implicit(app_fn(e))) {
        return pp_child(app_fn(e), bp);
    } else if (is_app(e) && !m_coercion && is_coercion(m_env, get_app_fn(e))) {
        return pp_child(app_arg(e), bp); // TODO(Fix): this is not correct for coercions to function-class
    } else {
        result r = pp(e);
        if (r.second < bp) {
            return mk_result(paren(r.first));
        } else {
            return r;
        }
    }
}

auto pretty_fn::pp_var(expr const & e) -> result {
    unsigned vidx = var_idx(e);
    return mk_result(compose(format("#"), format(vidx)));
}

auto pretty_fn::pp_sort(expr const & e) -> result {
    if (m_env.impredicative() && e == Bool) {
        return mk_result(format("Bool"));
    } else if (m_universes) {
        return mk_result(group(format({format("Type.{"), nest(6, pp_level(sort_level(e))), format("}")})));
    } else {
        return mk_result(format("Type"));
    }
}

auto pretty_fn::pp_const(expr const & e) -> result {
    name n = const_name(e);
    if (auto it = is_aliased(m_env, mk_constant(n))) { // TODO(Leo): fix is_aliased should get a name as argument
        n = *it;
    } else {
        for (name const & ns : get_namespaces(m_env)) {
            name new_n = n.replace_prefix(ns, name());
            if (new_n != n) {
                n = new_n;
                break;
            }
        }
    }
    if (m_universes) {
        format r = compose(format(n), format(".{"));
        for (auto const & l : const_levels(e)) {
            format l_fmt = pp_level(l);
            if (is_max(l) || is_imax(l))
                l_fmt = paren(l_fmt);
            r += nest(m_indent, compose(line(), l_fmt));
        }
        r += format("}");
        return mk_result(group(r));
    } else {
        return mk_result(format(n));
    }
}

auto pretty_fn::pp_meta(expr const & e) -> result {
    return mk_result(compose(format("?"), format(mlocal_name(e))));
}

auto pretty_fn::pp_local(expr const & e) -> result {
    return mk_result(format(local_pp_name(e)));
}

auto pretty_fn::pp_app(expr const & e) -> result {
    result res_fn = pp_child(app_fn(e), max_bp()-1);
    result res_arg = pp_child(app_arg(e), max_bp());
    return mk_result(group(compose(res_fn.first, nest(m_indent, compose(line(), res_arg.first)))), max_bp()-1);
}

format pretty_fn::pp_binder_block(buffer<name> const & names, expr const & type, binder_info const & bi) {
    format r;
    if (bi.is_implicit()) r += format("{");
    else if (bi.is_cast()) r += format("[");
    else if (bi.is_strict_implicit() && m_unicode) r += format("⦃");
    else if (bi.is_strict_implicit() && !m_unicode) r += format("{{");
    else r += format("(");
    for (name const & n : names) {
        r += format(n);
        r += space();
    }
    r += compose(colon(), nest(m_indent, compose(line(), pp_child(type, 0).first)));
    if (bi.is_implicit()) r += format("}");
    else if (bi.is_cast()) r += format("]");
    else if (bi.is_strict_implicit() && m_unicode) r += format("⦄");
    else if (bi.is_strict_implicit() && !m_unicode) r += format("}}");
    else r += format(")");
    return group(r);
}

format pretty_fn::pp_binders(buffer<expr> const & locals) {
    unsigned num     = locals.size();
    buffer<name> names;
    expr local       = locals[0];
    expr   type      = mlocal_type(local);
    binder_info bi   = local_info(local);
    names.push_back(local_pp_name(local));
    format r;
    for (unsigned i = 1; i < num; i++) {
        expr local = locals[i];
        if (mlocal_type(local) == type && local_info(local) == bi) {
            names.push_back(local_pp_name(local));
        } else {
            r += group(compose(line(), pp_binder_block(names, type, bi)));
            names.clear();
            type = mlocal_type(local);
            bi   = local_info(local);
            names.push_back(local_pp_name(local));
        }
    }
    r += group(compose(line(), pp_binder_block(names, type, bi)));
    return r;
}

auto pretty_fn::pp_lambda(expr const & e) -> result {
    expr b = e;
    buffer<expr> locals;
    while (is_lambda(b)) {
        auto p = binding_body_fresh(b, true);
        locals.push_back(p.second);
        b = p.first;
    }
    format r = m_unicode ? g_lambda_n_fmt : g_lambda_fmt;
    r += pp_binders(locals);
    r += compose(comma(), nest(m_indent, compose(line(), pp_child(b, 0).first)));
    return mk_result(r, 0);
}

auto pretty_fn::pp_pi(expr const & e) -> result {
    if (is_arrow(e)) {
        result lhs = pp_child(binding_domain(e), get_arrow_prec());
        result rhs = pp_child(lift_free_vars(binding_body(e), 1), get_arrow_prec()-1);
        format r   = group(format{lhs.first, space(), m_unicode ? g_arrow_n_fmt : g_arrow_fmt, line(), rhs.first});
        return mk_result(r, get_arrow_prec()-1);
    } else {
        expr b = e;
        buffer<expr> locals;
        while (is_pi(b) && !is_arrow(b)) {
            auto p = binding_body_fresh(b, true);
            locals.push_back(p.second);
            b = p.first;
        }
        format r;
        if (is_prop(b))
            r = m_unicode ? g_forall_n_fmt : g_forall_fmt;
        else
            r = m_unicode ? g_pi_n_fmt : g_pi_fmt;
        r += pp_binders(locals);
        r += compose(comma(), nest(m_indent, compose(line(), pp_child(b, 0).first)));
        return mk_result(r, 0);
    }
}

auto pretty_fn::pp_macro(expr const & e) -> result {
    // TODO(Leo): handle let, have macro annotations
    // fix macro<->pp interface
    format r = compose(format("["), format(macro_def(e).get_name()));
    for (unsigned i = 0; i < macro_num_args(e); i++)
        r += nest(m_indent, compose(line(), pp_child(macro_arg(e, i), max_bp()).first));
    r += format("]");
    return mk_result(group(r));
}

auto pretty_fn::pp(expr const & e) -> result {
    if (m_depth > m_max_depth || m_num_steps > m_max_steps)
        return mk_result(m_unicode ? g_ellipsis_n_fmt : g_ellipsis_fmt);
    flet<unsigned> let_d(m_depth, m_depth+1);
    m_num_steps++;

    switch (e.kind()) {
    case expr_kind::Var:       return pp_var(e);
    case expr_kind::Sort:      return pp_sort(e);
    case expr_kind::Constant:  return pp_const(e);
    case expr_kind::Meta:      return pp_meta(e);
    case expr_kind::Local:     return pp_local(e);
    case expr_kind::App:       return pp_app(e);
    case expr_kind::Lambda:    return pp_lambda(e);
    case expr_kind::Pi:        return pp_pi(e);
    case expr_kind::Macro:     return pp_macro(e);
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

pretty_fn::pretty_fn(environment const & env, options const & o):
    m_env(env), m_tc(env) {
    set_options(o);
    m_meta_prefix   = "M";
    m_next_meta_idx = 1;
}

format pretty_fn::operator()(expr const & e) {
    m_depth = 0; m_num_steps = 0;
    return pp_child(purify(e), 0).first;
}

class pretty_formatter_cell : public formatter_cell {
public:
    /** \brief Format the given expression. */
    virtual format operator()(environment const & env, expr const & e, options const & o) const {
        return pretty_fn(env, o)(e);
    }
};

formatter mk_pretty_formatter() {
    return mk_formatter(pretty_formatter_cell());
}
}