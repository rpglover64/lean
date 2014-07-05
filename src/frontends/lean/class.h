/*
Copyright (c) 2014 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include "util/name.h"
#include "util/list.h"
#include "kernel/environment.h"
#include "frontends/lean/cmd_table.h"

namespace lean {
/** \brief Add a new 'class' to the environment. */
environment add_class(environment const & env, name const & n);
/** \brief Add a new 'class instance' to the environment. */
environment add_instance(environment const & env, name const & n);
/** \brief Return the instances of the given class. */
list<name> get_class_instances(environment const & env, name const & c);

void register_class_cmds(cmd_table & r);
}