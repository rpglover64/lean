/*
Copyright (c) 2014 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <algorithm>
#include <string>
#include "util/sstream.h"
#include "util/sexpr/option_declarations.h"
#include "kernel/type_checker.h"
#include "kernel/abstract.h"
#include "kernel/instantiate.h"
#include "kernel/inductive/inductive.h"
#include "kernel/default_converter.h"
#include "library/io_state_stream.h"
#include "library/scoped_ext.h"
#include "library/aliases.h"
#include "library/protected.h"
#include "library/locals.h"
#include "library/coercion.h"
#include "library/reducible.h"
#include "library/normalize.h"
#include "library/print.h"
#include "library/class.h"
#include "library/flycheck.h"
#include "library/util.h"
#include "library/pp_options.h"
#include "library/definitional/projection.h"
#include "frontends/lean/util.h"
#include "frontends/lean/parser.h"
#include "frontends/lean/calc.h"
#include "frontends/lean/notation_cmd.h"
#include "frontends/lean/inductive_cmd.h"
#include "frontends/lean/structure_cmd.h"
#include "frontends/lean/migrate_cmd.h"
#include "frontends/lean/find_cmd.h"
#include "frontends/lean/begin_end_ext.h"
#include "frontends/lean/decl_cmds.h"
#include "frontends/lean/tactic_hint.h"
#include "frontends/lean/tokens.h"
#include "frontends/lean/parse_table.h"

namespace lean {
static void print_coercions(parser & p, optional<name> const & C) {
    environment const & env = p.env();
    options opts = p.regular_stream().get_options();
    opts = opts.update(get_pp_coercions_option_name(), true);
    io_state_stream out = p.regular_stream().update_options(opts);
    char const * arrow = get_pp_unicode(opts) ? "↣" : ">->";
    for_each_coercion_user(env, [&](name const & C1, name const & D, expr const & c, level_param_names const &, unsigned) {
            if (!C || *C == C1)
                out << C1 << " " << arrow << " " << D << " : " << c << endl;
        });
    for_each_coercion_sort(env, [&](name const & C1, expr const & c, level_param_names const &, unsigned) {
            if (!C || *C == C1)
                out << C1 << " " << arrow << " [sort-class] : " << c << endl;
        });
    for_each_coercion_fun(env, [&](name const & C1, expr const & c, level_param_names const &, unsigned) {
            if (!C || *C == C1)
                out << C1 << " " << arrow << " [fun-class] : " << c << endl;
        });
}

static void print_axioms(parser & p) {
    bool has_axioms = false;
    environment const & env = p.env();
    env.for_each_declaration([&](declaration const & d) {
            name const & n = d.get_name();
            if (!d.is_definition() &&
                !inductive::is_inductive_decl(env, n) &&
                !inductive::is_elim_rule(env, n) &&
                !inductive::is_intro_rule(env, n)) {
                p.regular_stream() << n << " : " << d.get_type() << endl;
                has_axioms = true;
            }
        });
    if (!has_axioms)
        p.regular_stream() << "no axioms" << endl;
}

static void print_prefix(parser & p) {
    name prefix = p.check_id_next("invalid 'print prefix' command, identifier expected");
    environment const & env = p.env();
    buffer<declaration> to_print;
    env.for_each_declaration([&](declaration const & d) {
            if (is_prefix_of(prefix, d.get_name())) {
                to_print.push_back(d);
            }
        });
    std::sort(to_print.begin(), to_print.end(), [](declaration const & d1, declaration const & d2) { return d1.get_name() < d2.get_name(); });
    for (declaration const & d : to_print) {
        p.regular_stream() << d.get_name() << " : " << d.get_type() << endl;
    }
    if (to_print.empty())
        p.regular_stream() << "no declaration starting with prefix '" << prefix << "'" << endl;
}

static void print_fields(parser & p) {
    auto pos = p.pos();
    environment const & env = p.env();
    name S = p.check_constant_next("invalid 'print fields' command, constant expected");
    if (!is_structure(env, S))
        throw parser_error(sstream() << "invalid 'print fields' command, '" << S << "' is not a structure", pos);
    buffer<name> field_names;
    get_structure_fields(env, S, field_names);
    for (name const & field_name : field_names) {
        declaration d = env.get(field_name);
        p.regular_stream() << d.get_name() << " : " << d.get_type() << endl;
    }
}

static bool uses_token(unsigned num, notation::transition const * ts, name const & token) {
    for (unsigned i = 0; i < num; i++) {
        if (ts[i].get_token() == token)
            return true;
    }
    return false;
}

static bool uses_some_token(unsigned num, notation::transition const * ts, buffer<name> const & tokens) {
    return
        tokens.empty() ||
        std::any_of(tokens.begin(), tokens.end(), [&](name const & token) { return uses_token(num, ts, token); });
}

static bool print_parse_table(parser & p, parse_table const & t, bool nud, buffer<name> const & tokens) {
    bool found = false;
    io_state ios = p.ios();
    options os   = ios.get_options();
    os = os.update_if_undef(get_pp_full_names_option_name(), true);
    os = os.update(get_pp_notation_option_name(), false);
    ios.set_options(os);
    optional<token_table> tt(get_token_table(p.env()));
    t.for_each([&](unsigned num, notation::transition const * ts, list<expr> const & overloads) {
            if (uses_some_token(num, ts, tokens)) {
                found = true;
                io_state_stream out = regular(p.env(), ios);
                notation::display(out, num, ts, overloads, nud, tt);
            }
        });
    return found;
}

static void print_notation(parser & p) {
    buffer<name> tokens;
    while (p.curr_is_keyword()) {
        tokens.push_back(p.get_token_info().token());
        p.next();
    }
    bool found = false;
    if (print_parse_table(p, get_nud_table(p.env()), true, tokens))
        found = true;
    if (print_parse_table(p, get_led_table(p.env()), false, tokens))
        found = true;
    if (!found)
        p.regular_stream() << "no notation" << endl;
}

static void print_metaclasses(parser & p) {
    buffer<name> c;
    get_metaclasses(c);
    for (name const & n : c)
        p.regular_stream() << "[" << n << "]" << endl;
}

environment print_cmd(parser & p) {
    flycheck_information info(p.regular_stream());
    if (info.enabled()) {
        p.display_information_pos(p.cmd_pos());
        p.regular_stream() << "print result:\n";
    }
    if (p.curr() == scanner::token_kind::String) {
        p.regular_stream() << p.get_str_val() << endl;
        p.next();
    } else if (p.curr_is_token_or_id(get_raw_tk())) {
        p.next();
        expr e = p.parse_expr();
        io_state_stream out = p.regular_stream();
        options opts = out.get_options();
        opts = opts.update(get_pp_notation_option_name(), false);
        out.update_options(opts) << e << endl;
    } else if (p.curr_is_token_or_id(get_options_tk())) {
        p.next();
        p.regular_stream() << p.ios().get_options() << endl;
    } else if (p.curr_is_token_or_id(get_trust_tk())) {
        p.next();
        p.regular_stream() << "trust level: " << p.env().trust_lvl() << endl;
    } else if (p.curr_is_token_or_id(get_definition_tk())) {
        p.next();
        auto pos = p.pos();
        name c = p.check_constant_next("invalid 'print definition', constant expected");
        environment const & env = p.env();
        declaration d = env.get(c);
        io_state_stream out = p.regular_stream();
        options opts        = out.get_options();
        opts                = opts.update_if_undef(get_pp_beta_name(), false);
        io_state_stream new_out = out.update_options(opts);
        if (!d.is_definition())
            throw parser_error(sstream() << "invalid 'print definition', '" << c << "' is not a definition", pos);
        new_out << d.get_value() << endl;
    } else if (p.curr_is_token_or_id(get_instances_tk())) {
        p.next();
        name c = p.check_constant_next("invalid 'print instances', constant expected");
        environment const & env = p.env();
        for (name const & i : get_class_instances(env, c)) {
            p.regular_stream() << i << " : " << env.get(i).get_type() << endl;
        }
    } else if (p.curr_is_token_or_id(get_classes_tk())) {
        p.next();
        environment const & env = p.env();
        buffer<name> classes;
        get_classes(env, classes);
        std::sort(classes.begin(), classes.end());
        for (name const & c : classes) {
            p.regular_stream() << c << " : " << env.get(c).get_type() << endl;
        }
    } else if (p.curr_is_token_or_id(get_prefix_tk())) {
        p.next();
        print_prefix(p);
    } else if (p.curr_is_token_or_id(get_coercions_tk())) {
        p.next();
        optional<name> C;
        if (p.curr_is_identifier())
            C = p.check_constant_next("invalid 'print coercions', constant expected");
        print_coercions(p, C);
    } else if (p.curr_is_token_or_id(get_metaclasses_tk())) {
        p.next();
        print_metaclasses(p);
    } else if (p.curr_is_token_or_id(get_axioms_tk())) {
        p.next();
        print_axioms(p);
    } else if (p.curr_is_token_or_id(get_fields_tk())) {
        p.next();
        print_fields(p);
    } else if (p.curr_is_token_or_id(get_notation_tk())) {
        p.next();
        print_notation(p);
    } else {
        throw parser_error("invalid print command", p.pos());
    }
    return p.env();
}

environment section_cmd(parser & p) {
    name n;
    if (p.curr_is_identifier())
        n = p.check_atomic_id_next("invalid section, atomic identifier expected");
    p.push_local_scope();
    return push_scope(p.env(), p.ios(), scope_kind::Section, n);
}

environment context_cmd(parser & p) {
    name n;
    if (p.curr_is_identifier())
        n = p.check_atomic_id_next("invalid context, atomic identifier expected");
    bool save_options = true;
    p.push_local_scope(save_options);
    return push_scope(p.env(), p.ios(), scope_kind::Context, n);
}

environment namespace_cmd(parser & p) {
    auto pos = p.pos();
    name n = p.check_atomic_id_next("invalid namespace declaration, atomic identifier expected");
    if (is_root_namespace(n))
        throw parser_error(sstream() << "invalid namespace name, '" << n << "' is reserved", pos);
    p.push_local_scope();
    return push_scope(p.env(), p.ios(), scope_kind::Namespace, n);
}

static void redeclare_aliases(parser & p,
                              list<pair<name, level>> old_level_entries,
                              list<pair<name, expr>> old_entries) {
    environment const & env = p.env();
    if (!in_context(env))
        return;
    list<pair<name, expr>> new_entries = p.get_local_entries();
    buffer<pair<name, expr>> to_redeclare;
    name_set popped_locals;
    while (!is_eqp(old_entries, new_entries)) {
        pair<name, expr> entry = head(old_entries);
        if (is_local_ref(entry.second))
            to_redeclare.push_back(entry);
        else if (is_local(entry.second))
            popped_locals.insert(mlocal_name(entry.second));
        old_entries = tail(old_entries);
    }
    name_set popped_levels;
    list<pair<name, level>> new_level_entries = p.get_local_level_entries();
    while (!is_eqp(old_level_entries, new_level_entries)) {
        level const & l = head(old_level_entries).second;
        if (is_param(l))
            popped_levels.insert(param_id(l));
        old_level_entries = tail(old_level_entries);
    }

    for (auto const & entry : to_redeclare) {
        expr new_ref = update_local_ref(entry.second, popped_levels, popped_locals);
        if (!is_constant(new_ref))
            p.add_local_expr(entry.first, new_ref);
    }
}

environment end_scoped_cmd(parser & p) {
    list<pair<name, level>> level_entries = p.get_local_level_entries();
    list<pair<name, expr>> entries        = p.get_local_entries();
    p.pop_local_scope();
    if (p.curr_is_identifier()) {
        name n = p.check_atomic_id_next("invalid end of scope, atomic identifier expected");
        environment env = pop_scope(p.env(), n);
        redeclare_aliases(p, level_entries, entries);
        return env;
    } else {
        environment env = pop_scope(p.env());
        redeclare_aliases(p, level_entries, entries);
        return env;
    }
}

environment check_cmd(parser & p) {
    expr e; level_param_names ls;
    std::tie(e, ls) = parse_local_expr(p);
    auto tc = mk_type_checker(p.env(), p.mk_ngen(), true);
    expr type = tc->check(e, ls).first;
    auto reg              = p.regular_stream();
    formatter fmt         = reg.get_formatter();
    options opts          = p.ios().get_options();
    opts                  = opts.update_if_undef(get_pp_metavar_args_name(), true);
    fmt                   = fmt.update_options(opts);
    unsigned indent       = get_pp_indent(opts);
    format r = group(fmt(e) + space() + colon() + nest(indent, line() + fmt(type)));
    flycheck_information info(p.regular_stream());
    if (info.enabled()) {
        p.display_information_pos(p.cmd_pos());
        p.regular_stream() << "check result:\n";
    }
    reg << mk_pair(r, opts) << endl;
    return p.env();
}

class all_transparent_converter : public default_converter {
public:
    all_transparent_converter(environment const & env):
        default_converter(env, optional<module_idx>(), true) {
    }
    virtual bool is_opaque(declaration const &) const {
        return false;
    }
};

environment eval_cmd(parser & p) {
    bool whnf   = false;
    bool all_transparent = false;
    if (p.curr_is_token(get_whnf_tk())) {
        p.next();
        whnf = true;
    } else if (p.curr_is_token(get_all_transparent_tk())) {
        p.next();
        all_transparent = true;
    }
    expr e; level_param_names ls;
    std::tie(e, ls) = parse_local_expr(p);
    expr r;
    if (whnf) {
        auto tc = mk_type_checker(p.env(), p.mk_ngen(), true);
        r = tc->whnf(e).first;
    } else if (all_transparent) {
        type_checker tc(p.env(), name_generator(),
                        std::unique_ptr<converter>(new all_transparent_converter(p.env())));
        r = normalize(tc, ls, e);
    } else {
        r = normalize(p.env(), ls, e);
    }
    flycheck_information info(p.regular_stream());
    if (info.enabled()) {
        p.display_information_pos(p.cmd_pos());
        p.regular_stream() << "eval result:\n";
    }
    p.regular_stream() << r << endl;
    return p.env();
}

environment exit_cmd(parser &) {
    throw interrupt_parser();
}

environment set_option_cmd(parser & p) {
    auto id_pos = p.pos();
    name id = p.check_id_next("invalid set option, identifier (i.e., option name) expected");
    auto decl_it = get_option_declarations().find(id);
    if (decl_it == get_option_declarations().end()) {
        // add "lean" prefix
        name lean_id = name("lean") + id;
        decl_it = get_option_declarations().find(lean_id);
        if (decl_it == get_option_declarations().end()) {
            throw parser_error(sstream() << "unknown option '" << id
                               << "', type 'help options.' for list of available options", id_pos);
        } else {
            id = lean_id;
        }
    }
    option_kind k = decl_it->second.kind();
    if (k == BoolOption) {
        if (p.curr_is_token_or_id(get_true_tk()))
            p.set_option(id, true);
        else if (p.curr_is_token_or_id(get_false_tk()))
            p.set_option(id, false);
        else
            throw parser_error("invalid Boolean option value, 'true' or 'false' expected", p.pos());
        p.next();
    } else if (k == StringOption) {
        if (!p.curr_is_string())
            throw parser_error("invalid option value, given option is not a string", p.pos());
        p.set_option(id, p.get_str_val());
        p.next();
    } else if (k == DoubleOption) {
        p.set_option(id, p.parse_double());
    } else if (k == UnsignedOption || k == IntOption) {
        p.set_option(id, p.parse_small_nat());
    } else {
        throw parser_error("invalid option value, 'true', 'false', string, integer or decimal value expected", p.pos());
    }
    p.updt_options();
    environment env = p.env();
    return update_fingerprint(env, p.get_options().hash());
}

static name parse_metaclass(parser & p) {
    if (p.curr_is_token(get_lbracket_tk())) {
        p.next();
        auto pos = p.pos();
        name n;
        while (!p.curr_is_token(get_rbracket_tk())) {
            if (p.curr_is_identifier())
                n = n.append_after(p.get_name_val().to_string().c_str());
            else if (p.curr_is_keyword() || p.curr_is_command())
                n = n.append_after(p.get_token_info().value().to_string().c_str());
            else if (p.curr_is_token(get_sub_tk()))
                n = n.append_after("-");
            else
                throw parser_error("invalid 'open' command, identifier or symbol expected", pos);
            p.next();
        }
        p.check_token_next(get_rbracket_tk(), "invalid 'open' command, ']' expected");
        if (!is_metaclass(n) && n != get_decls_tk() && n != get_declarations_tk())
            throw parser_error(sstream() << "invalid metaclass name '[" << n << "]'", pos);
        return n;
    } else {
        return name();
    }
}

static void parse_metaclasses(parser & p, buffer<name> & r) {
    if (p.curr_is_token(get_sub_tk())) {
        p.next();
        buffer<name> tmp;
        get_metaclasses(tmp);
        tmp.push_back(get_decls_tk());
        while (p.curr_is_token(get_lbracket_tk())) {
            name m = parse_metaclass(p);
            tmp.erase_elem(m);
        }
        r.append(tmp);
    } else {
        while (p.curr_is_token(get_lbracket_tk())) {
            r.push_back(parse_metaclass(p));
        }
    }
}

static void check_identifier(parser & p, environment const & env, name const & ns, name const & id) {
    name full_id = ns + id;
    if (!env.find(full_id))
        throw parser_error(sstream() << "invalid 'open' command, unknown declaration '" << full_id << "'", p.pos());
}

// add id as an abbreviation for d
static environment add_abbrev(parser & p, environment const & env, name const & id, name const & d) {
    declaration decl = env.get(d);
    buffer<level> ls;
    for (name const & l : decl.get_univ_params())
        ls.push_back(mk_param_univ(l));
    expr value  = mk_constant(d, to_list(ls.begin(), ls.end()));
    bool opaque = false;
    name const & ns = get_namespace(env);
    name full_id    = ns + id;
    p.add_abbrev_index(full_id, d);
    environment new_env =
        module::add(env, check(env, mk_definition(env, full_id, decl.get_univ_params(), decl.get_type(), value, opaque)));
    if (full_id != id)
        new_env = add_expr_alias_rec(new_env, id, full_id);
    return new_env;
}

// open/export [class] id (as id)? (id ...) (renaming id->id id->id) (hiding id ... id)
environment open_export_cmd(parser & p, bool open) {
    environment env = p.env();
    while (true) {
        buffer<name> metacls;
        parse_metaclasses(p, metacls);
        bool decls = false;
        if (metacls.empty() ||
            std::find(metacls.begin(), metacls.end(), get_decls_tk()) != metacls.end() ||
            std::find(metacls.begin(), metacls.end(), get_declarations_tk()) != metacls.end())
            decls = true;
        auto pos   = p.pos();
        name ns    = p.check_id_next("invalid 'open/export' command, identifier expected");
        optional<name> real_ns = to_valid_namespace_name(env, ns);
        if (!real_ns)
            throw parser_error(sstream() << "invalid namespace name '" << ns << "'", pos);
        ns = *real_ns;
        name as;
        if (p.curr_is_token_or_id(get_as_tk())) {
            p.next();
            as = p.check_id_next("invalid 'open/export' command, identifier expected");
        }
        if (open)
            env = using_namespace(env, p.ios(), ns, metacls);
        else
            env = export_namespace(env, p.ios(), ns, metacls);
        if (decls) {
            // Remark: we currently to not allow renaming and hiding of universe levels
            buffer<name> exceptions;
            bool found_explicit = false;
            while (p.curr_is_token(get_lparen_tk())) {
                p.next();
                if (p.curr_is_token_or_id(get_renaming_tk())) {
                    p.next();
                    while (p.curr_is_identifier()) {
                        name from_id = p.get_name_val();
                        p.next();
                        p.check_token_next(get_arrow_tk(), "invalid 'open/export' command renaming, '->' expected");
                        name to_id = p.check_id_next("invalid 'open/export' command renaming, identifier expected");
                        check_identifier(p, env, ns, from_id);
                        exceptions.push_back(from_id);
                        if (open)
                            env = add_expr_alias(env, as+to_id, ns+from_id);
                        else
                            env = add_abbrev(p, env, as+to_id, ns+from_id);
                    }
                } else if (p.curr_is_token_or_id(get_hiding_tk())) {
                    p.next();
                    while (p.curr_is_identifier()) {
                        name id = p.get_name_val();
                        p.next();
                        check_identifier(p, env, ns, id);
                        exceptions.push_back(id);
                    }
                } else if (p.curr_is_identifier()) {
                    found_explicit = true;
                    while (p.curr_is_identifier()) {
                        name id = p.get_name_val();
                        p.next();
                        check_identifier(p, env, ns, id);
                        if (open)
                            env = add_expr_alias(env, as+id, ns+id);
                        else
                            env = add_abbrev(p, env, as+id, ns+id);
                    }
                } else {
                    throw parser_error("invalid 'open/export' command option, "
                                       "identifier, 'hiding' or 'renaming' expected", p.pos());
                }
                if (found_explicit && !exceptions.empty())
                    throw parser_error("invalid 'open/export' command option, "
                                       "mixing explicit and implicit 'open/export' options", p.pos());
                p.check_token_next(get_rparen_tk(), "invalid 'open/export' command option, ')' expected");
            }
            if (!found_explicit) {
                if (open) {
                    env = add_aliases(env, ns, as, exceptions.size(), exceptions.data());
                } else {
                    environment new_env = env;
                    env.for_each_declaration([&](declaration const & d) {
                            if (!is_protected(env, d.get_name()) &&
                                is_prefix_of(ns, d.get_name()) &&
                                !is_exception(d.get_name(), ns, exceptions.size(), exceptions.data())) {
                                name new_id = d.get_name().replace_prefix(ns, as);
                                if (!new_id.is_anonymous())
                                    new_env = add_abbrev(p, new_env, new_id, d.get_name());
                            }
                        });
                    env = new_env;
                }
            }
        }
        if (!p.curr_is_token(get_lbracket_tk()) && !p.curr_is_identifier())
            break;
    }
    return env;
}
environment open_cmd(parser & p) { return open_export_cmd(p, true); }
environment export_cmd(parser & p) { return open_export_cmd(p, false); }

environment erase_cache_cmd(parser & p) {
    name n = p.check_id_next("invalid #erase_cache command, identifier expected");
    p.erase_cached_definition(n);
    return p.env();
}

environment projections_cmd(parser & p) {
    name n = p.check_id_next("invalid #projections command, identifier expected");
    if (p.curr_is_token(get_dcolon_tk())) {
        p.next();
        buffer<name> proj_names;
        while (p.curr_is_identifier()) {
            proj_names.push_back(n + p.get_name_val());
            p.next();
        }
        return mk_projections(p.env(), n, proj_names);
    } else {
        return mk_projections(p.env(), n);
    }
}

environment telescope_eq_cmd(parser & p) {
    expr e; level_param_names ls;
    std::tie(e, ls) = parse_local_expr(p);
    buffer<expr> t;
    while (is_pi(e)) {
        expr local = mk_local(p.mk_fresh_name(), binding_name(e), binding_domain(e), binder_info());
        t.push_back(local);
        e = instantiate(binding_body(e), local);
    }
    auto tc = mk_type_checker(p.env(), p.mk_ngen(), true);
    buffer<expr> eqs;
    mk_telescopic_eq(*tc, t, eqs);
    for (expr const & eq : eqs) {
        regular(p.env(), p.ios()) << local_pp_name(eq) << " : " << mlocal_type(eq) << "\n";
        tc->check(mlocal_type(eq), ls);
    }
    return p.env();
}

environment local_cmd(parser & p) {
    if (p.curr_is_token_or_id(get_attribute_tk())) {
        p.next();
        return local_attribute_cmd(p);
    } else if (p.curr_is_token(get_abbreviation_tk())) {
        p.next();
        return local_abbreviation_cmd(p);
    } else {
        return local_notation_cmd(p);
    }
}

static environment help_cmd(parser & p) {
    flycheck_information info(p.regular_stream());
    if (info.enabled()) {
        p.display_information_pos(p.cmd_pos());
        p.regular_stream() << "help result:\n";
    }
    if (p.curr_is_token_or_id(get_options_tk())) {
        p.next();
        for (auto odecl : get_option_declarations()) {
            auto opt = odecl.second;
            regular(p.env(), p.ios())
                << "  " << opt.get_name() << " (" << opt.kind() << ") "
                << opt.get_description() << " (default: " << opt.get_default_value() << ")" << endl;
        }
    } else if (p.curr_is_token_or_id(get_commands_tk())) {
        p.next();
        buffer<name> ns;
        cmd_table const & cmds = p.cmds();
        cmds.for_each([&](name const & n, cmd_info const &) {
                ns.push_back(n);
            });
        std::sort(ns.begin(), ns.end());
        for (name const & n : ns) {
            regular(p.env(), p.ios())
                << "  " << n << ": " << cmds.find(n)->get_descr() << endl;
        };
    } else {
        p.regular_stream()
            << "help options  : describe available options\n"
            << "help commands : describe available commands\n";
    }
    return p.env();
}

void init_cmd_table(cmd_table & r) {
    add_cmd(r, cmd_info("open",          "create aliases for declarations, and use objects defined in other namespaces",
                        open_cmd));
    add_cmd(r, cmd_info("export",        "create abbreviations for declarations, "
                        "and export objects defined in other namespaces", export_cmd));
    add_cmd(r, cmd_info("set_option",    "set configuration option", set_option_cmd));
    add_cmd(r, cmd_info("exit",          "exit", exit_cmd));
    add_cmd(r, cmd_info("print",         "print a string", print_cmd));
    add_cmd(r, cmd_info("section",       "open a new section", section_cmd));
    add_cmd(r, cmd_info("context",       "open a new context", context_cmd));
    add_cmd(r, cmd_info("namespace",     "open a new namespace", namespace_cmd));
    add_cmd(r, cmd_info("end",           "close the current namespace/section", end_scoped_cmd));
    add_cmd(r, cmd_info("check",         "type check given expression, and display its type", check_cmd));
    add_cmd(r, cmd_info("eval",          "evaluate given expression", eval_cmd));
    add_cmd(r, cmd_info("find_decl",     "find definitions and/or theorems", find_cmd));
    add_cmd(r, cmd_info("local",         "define local attributes or notation", local_cmd));
    add_cmd(r, cmd_info("help",          "brief description of available commands and options", help_cmd));
    add_cmd(r, cmd_info("#erase_cache",  "erase cached definition (for debugging purposes)", erase_cache_cmd));
    add_cmd(r, cmd_info("#projections",  "generate projections for inductive datatype (for debugging purposes)", projections_cmd));
    add_cmd(r, cmd_info("#telescope_eq", "(for debugging purposes)", telescope_eq_cmd));

    register_decl_cmds(r);
    register_inductive_cmd(r);
    register_structure_cmd(r);
    register_migrate_cmd(r);
    register_notation_cmds(r);
    register_calc_cmds(r);
    register_begin_end_cmds(r);
    register_tactic_hint_cmd(r);
}

static cmd_table * g_cmds = nullptr;

cmd_table get_builtin_cmds() {
    return *g_cmds;
}

void initialize_builtin_cmds() {
    g_cmds = new cmd_table();
    init_cmd_table(*g_cmds);
}

void finalize_builtin_cmds() {
    delete g_cmds;
}
}
