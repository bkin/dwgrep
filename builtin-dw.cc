/*
   Copyright (C) 2014 Red Hat, Inc.
   This file is part of dwgrep.

   This file is free software; you can redistribute it and/or modify
   it under the terms of either

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at
       your option) any later version

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at
       your option) any later version

   or both in parallel, as here.

   dwgrep is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see <http://www.gnu.org/licenses/>.  */

#include <memory>
#include "make_unique.hh"
#include <sstream>

#include "atval.hh"
#include "builtin-cst.hh"
#include "builtin-value.hh"
#include "builtin.hh"
#include "dwcst.hh"
#include "dwit.hh"
#include "dwpp.hh"
#include "known-dwarf.h"
#include "op.hh"
#include "overload.hh"
#include "value-dw.hh"

namespace
{
  struct builtin_winfo
    : public builtin
  {
    struct winfo
      : public op
    {
      std::shared_ptr <op> m_upstream;
      dwgrep_graph::sptr m_gr;
      all_dies_iterator m_it;
      valfile::uptr m_vf;
      size_t m_pos;

      winfo (std::shared_ptr <op> upstream, dwgrep_graph::sptr gr)
	: m_upstream {upstream}
	, m_gr {gr}
	, m_it {all_dies_iterator::end ()}
	, m_pos {0}
      {}

      void
      reset_me ()
      {
	m_vf = nullptr;
	m_pos = 0;
      }

      valfile::uptr
      next () override
      {
	while (true)
	  {
	    if (m_vf == nullptr)
	      {
		m_vf = m_upstream->next ();
		if (m_vf == nullptr)
		  return nullptr;
		m_it = all_dies_iterator (&*m_gr->dwarf);
	      }

	    if (m_it != all_dies_iterator::end ())
	      {
		auto ret = std::make_unique <valfile> (*m_vf);
		auto v = std::make_unique <value_die> (m_gr, **m_it, m_pos++);
		ret->push (std::move (v));
		++m_it;
		return ret;
	      }

	    reset_me ();
	  }
      }

      void
      reset () override
      {
	reset_me ();
	m_upstream->reset ();
      }

      std::string
      name () const override
      {
	return "winfo";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <winfo> (upstream, q);
    }

    char const *
    name () const override
    {
      return "winfo";
    }
  };
}

namespace
{
  struct builtin_unit
    : public builtin
  {
    struct unit
      : public op
    {
      std::shared_ptr <op> m_upstream;
      dwgrep_graph::sptr m_gr;
      valfile::uptr m_vf;
      all_dies_iterator m_it;
      all_dies_iterator m_end;
      size_t m_pos;

      unit (std::shared_ptr <op> upstream, dwgrep_graph::sptr gr)
	: m_upstream {upstream}
	, m_gr {gr}
	, m_it {all_dies_iterator::end ()}
	, m_end {all_dies_iterator::end ()}
	, m_pos {0}
      {}

      void
      init_from_die (Dwarf_Die die)
      {
	Dwarf_Die cudie;
	if (dwarf_diecu (&die, &cudie, nullptr, nullptr) == nullptr)
	  throw_libdw ();

	cu_iterator cuit {&*m_gr->dwarf, cudie};
	m_it = all_dies_iterator (cuit);
	m_end = all_dies_iterator (++cuit);
      }

      void
      reset_me ()
      {
	m_vf = nullptr;
	m_it = all_dies_iterator::end ();
	m_pos = 0;
      }

      valfile::uptr
      next () override
      {
	while (true)
	  {
	    while (m_vf == nullptr)
	      {
		if (auto vf = m_upstream->next ())
		  {
		    auto vp = vf->pop ();
		    if (auto v = value::as <value_die> (&*vp))
		      {
			init_from_die (v->get_die ());
			m_vf = std::move (vf);
		      }
		    else if (auto v = value::as <value_attr> (&*vp))
		      {
			init_from_die (v->get_die ());
			m_vf = std::move (vf);
		      }
		    else
		      show_expects (name (),
				    {value_die::vtype, value_attr::vtype});
		  }
		else
		  return nullptr;
	      }

	    if (m_it != m_end)
	      {
		auto ret = std::make_unique <valfile> (*m_vf);
		ret->push (std::make_unique <value_die>
				(m_gr, **m_it, m_pos++));
		++m_it;
		return ret;
	      }

	    reset_me ();
	  }
      }

      void
      reset () override
      {
	reset_me ();
	m_upstream->reset ();
      }

      std::string
      name () const override
      {
	return "unit";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <unit> (upstream, q);
    }

    char const *
    name () const override
    {
      return "unit";
    }
  };
}

namespace
{
  struct builtin_child
    : public builtin
  {
    struct child
      : public op
    {
      std::shared_ptr <op> m_upstream;
      dwgrep_graph::sptr m_gr;
      valfile::uptr m_vf;
      Dwarf_Die m_child;

      size_t m_pos;

      child (std::shared_ptr <op> upstream, dwgrep_graph::sptr gr)
	: m_upstream {upstream}
	, m_gr {gr}
	, m_child {}
	, m_pos {0}
      {}

      void
      reset_me ()
      {
	m_vf = nullptr;
	m_pos = 0;
      }

      valfile::uptr
      next () override
      {
	while (true)
	  {
	    while (m_vf == nullptr)
	      {
		if (auto vf = m_upstream->next ())
		  {
		    auto vp = vf->pop ();
		    if (auto v = value::as <value_die> (&*vp))
		      {
			Dwarf_Die *die = &v->get_die ();
			if (dwarf_haschildren (die))
			  {
			    if (dwarf_child (die, &m_child) != 0)
			      throw_libdw ();

			    // We found our guy.
			    m_vf = std::move (vf);
			  }
		      }
		    else
		      show_expects (name (), {value_die::vtype});
		  }
		else
		  return nullptr;
	      }

	    auto ret = std::make_unique <valfile> (*m_vf);
	    ret->push (std::make_unique <value_die> (m_gr, m_child, m_pos++));

	    switch (dwarf_siblingof (&m_child, &m_child))
	      {
	      case -1:
		throw_libdw ();
	      case 1:
		// No more siblings.
		reset_me ();
		break;
	      case 0:
		break;
	      }

	    return ret;
	  }
      }

      void
      reset () override
      {
	reset_me ();
	m_upstream->reset ();
      }

      std::string
      name () const override
      {
	return "child";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <child> (upstream, q);
    }

    char const *
    name () const override
    {
      return "child";
    }
  };
}

namespace
{
  struct builtin_attribute
    : public builtin
  {
    struct attribute
      : public op
    {
      std::shared_ptr <op> m_upstream;
      dwgrep_graph::sptr m_gr;
      Dwarf_Die m_die;
      valfile::uptr m_vf;
      attr_iterator m_it;

      size_t m_pos;

      attribute (std::shared_ptr <op> upstream, dwgrep_graph::sptr gr)
	: m_upstream {upstream}
	, m_gr {gr}
	, m_die {}
	, m_it {attr_iterator::end ()}
	, m_pos {0}
      {}

      void
      reset_me ()
      {
	m_vf = nullptr;
	m_pos = 0;
      }

      valfile::uptr
      next ()
      {
	while (true)
	  {
	    while (m_vf == nullptr)
	      {
		if (auto vf = m_upstream->next ())
		  {
		    auto vp = vf->pop ();
		    if (auto v = value::as <value_die> (&*vp))
		      {
			m_die = v->get_die ();
			m_it = attr_iterator (&m_die);
			m_vf = std::move (vf);
		      }
		    else
		      show_expects (name (), {value_die::vtype});
		  }
		else
		  return nullptr;
	      }

	    if (m_it != attr_iterator::end ())
	      {
		auto ret = std::make_unique <valfile> (*m_vf);
		ret->push (std::make_unique <value_attr>
			   (m_gr, **m_it, m_die, m_pos++));
		++m_it;
		return ret;
	      }

	    reset_me ();
	  }
      }

      void
      reset ()
      {
	reset_me ();
	m_upstream->reset ();
      }

      std::string
      name () const override
      {
	return "attribute";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <attribute> (upstream, q);
    }

    char const *
    name () const override
    {
      return "attribute";
    }
  };
}

namespace
{
  class dwop_f
    : public op
  {
    std::shared_ptr <op> m_upstream;

  protected:
    dwgrep_graph::sptr m_g;

  public:
    dwop_f (std::shared_ptr <op> upstream, dwgrep_graph::sptr gr)
      : m_upstream {upstream}
      , m_g {gr}
    {}

    valfile::uptr
    next () override final
    {
      while (auto vf = m_upstream->next ())
	{
	  auto vp = vf->pop ();
	  if (auto v = value::as <value_die> (&*vp))
	    {
	      if (operate (*vf, v->get_die ()))
		return vf;
	    }

	  else if (auto v = value::as <value_attr> (&*vp))
	    {
	      if (operate (*vf, v->get_attr (), v->get_die ()))
		return vf;
	    }

	  else if (auto v = value::as <value_loclist_op> (&*vp))
	    {
	      if (operate (*vf, v->get_dwop (), v->get_attr ()))
		return vf;
	    }

	  else
	    show_expects (name (),
			  {value_die::vtype, value_attr::vtype,
			   value_loclist_op::vtype});
	}

      return nullptr;
    }

    void reset () override final
    { m_upstream->reset (); }

    virtual std::string name () const override = 0;

    virtual bool operate (valfile &vf, Dwarf_Die &die)
    { return false; }

    virtual bool operate (valfile &vf, Dwarf_Attribute &attr, Dwarf_Die &die)
    { return false; }

    virtual bool operate (valfile &vf, Dwarf_Op *op, Dwarf_Attribute &attr)
    { return false; }
  };
}

namespace
{
  struct builtin_offset
    : public builtin
  {
    struct offset
      : public dwop_f
    {
      using dwop_f::dwop_f;

      bool
      operate (valfile &vf, Dwarf_Die &die) override
      {
	Dwarf_Off off = dwarf_dieoffset (&die);
	auto cst = constant {off, &hex_constant_dom};
	vf.push (std::make_unique <value_cst> (cst, 0));
	return true;
      }

      bool
      operate (valfile &vf, value_loclist_op &op)
      {
	Dwarf_Op const *dwop = op.get_dwop ();
	constant c {dwop->offset, &hex_constant_dom};
	vf.push (std::make_unique <value_cst> (c, 0));
	return true;
      }

      std::string
      name () const override
      {
	return "offset";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <offset> (upstream, q);
    }

    char const *
    name () const override
    {
      return "offset";
    }
  };
}

namespace
{
  struct builtin_label
    : public builtin
  {
    struct op
      : public dwop_f
    {
      using dwop_f::dwop_f;

      bool
      operate (valfile &vf, Dwarf_Die &die) override
      {
	int tag = dwarf_tag (&die);
	assert (tag >= 0);
	constant cst {(unsigned) tag, &dw_tag_dom};
	vf.push (std::make_unique <value_cst> (cst, 0));
	return true;
      }

      bool
      operate (valfile &vf, Dwarf_Attribute &attr, Dwarf_Die &die) override
      {
	unsigned name = dwarf_whatattr (&attr);
	constant cst {name, &dw_attr_dom};
	vf.push (std::make_unique <value_cst> (cst, 0));
	return true;
      }

      bool
      operate (valfile &vf, Dwarf_Op *op, Dwarf_Attribute &attr)
      {
	constant cst {op->atom, &dw_locexpr_opcode_short_dom};
	vf.push (std::make_unique <value_cst> (cst, 0));
	return true;
      }

      std::string
      name () const override
      {
	return "label";
      }
    };

    std::shared_ptr < ::op>
    build_exec (std::shared_ptr < ::op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <op> (upstream, q);
    }

    char const *
    name () const override
    {
      return "label";
    }
  };
}

namespace
{
  struct builtin_form
    : public builtin
  {
    struct form
      : public dwop_f
    {
      using dwop_f::dwop_f;

      bool
      operate (valfile &vf, Dwarf_Attribute &attr, Dwarf_Die &die) override
      {
	unsigned name = dwarf_whatform (&attr);
	constant cst {name, &dw_form_dom};
	vf.push (std::make_unique <value_cst> (cst, 0));
	return true;
      }

      std::string
      name () const override
      {
	return "form";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <form> (upstream, q);
    }

    char const *
    name () const override
    {
      return "form";
    }
  };
}

namespace
{
  struct builtin_parent
    : public builtin
  {
    struct parent
      : public dwop_f
    {
      using dwop_f::dwop_f;

      bool
      operate (valfile &vf, Dwarf_Die &die) override
      {
	Dwarf_Off par_off = m_g->find_parent (die);
	if (par_off == dwgrep_graph::none_off)
	  return false;

	Dwarf_Die par_die;
	if (dwarf_offdie (&*m_g->dwarf, par_off, &par_die) == nullptr)
	  throw_libdw ();

	vf.push (std::make_unique <value_die> (m_g, par_die, 0));
	return true;
      }

      bool
      operate (valfile &vf, Dwarf_Attribute &attr, Dwarf_Die &die) override
      {
	vf.push (std::make_unique <value_die> (m_g, die, 0));
	return true;
      }

      std::string
      name () const override
      {
	return "parent";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <parent> (upstream, q);
    }

    char const *
    name () const override
    {
      return "parent";
    }
  };
}

namespace
{
  struct builtin_integrate
    : public builtin
  {
    struct integrate
      : public dwop_f
    {
      using dwop_f::dwop_f;

      bool
      operate (valfile &vf, Dwarf_Die &die) override
      {
	Dwarf_Attribute attr_mem, *attr
	  = dwarf_attr (&die, DW_AT_abstract_origin, &attr_mem);

	if (attr == nullptr)
	  attr = dwarf_attr (&die, DW_AT_specification, &attr_mem);

	if (attr == nullptr)
	  return false;

	Dwarf_Die die_mem, *die2 = dwarf_formref_die (attr, &die_mem);
	if (die2 == nullptr)
	  throw_libdw ();

	vf.push (std::make_unique <value_die> (m_g, *die2, 0));
	return true;
      }

      std::string
      name () const override
      {
	return "integrate";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <integrate> (upstream, q);
    }

    char const *
    name () const override
    {
      return "integrate";
    }
  };
}

namespace
{
  struct op_loclist_op
    : public inner_op
  {
    typedef std::function <std::unique_ptr <value_producer>
			   (value_loclist_op &, dwgrep_graph::sptr gr)> cb_t;
    dwgrep_graph::sptr m_gr;
    cb_t m_cb;
    char const *m_name;
    std::unique_ptr <value_producer> m_vp;
    valfile::uptr m_vf;

    op_loclist_op (std::shared_ptr <op> upstream, dwgrep_graph::sptr gr,
		   cb_t cb, char const *name)
      : inner_op {upstream}
      , m_gr {gr}
      , m_cb {cb}
      , m_name {name}
    {}

    void
    reset_me ()
    {
      m_vp = nullptr;
      m_vf = nullptr;
    }

    valfile::uptr
    next () override final
    {
      while (true)
	{
	  while (m_vp == nullptr)
	    if (auto vf = m_upstream->next ())
	      {
		auto v = vf->pop ();
		if (auto vlo = value::as <value_loclist_op> (&*v))
		  {
		    m_vp = m_cb (*vlo, m_gr);
		    m_vf = std::move (vf);
		  }
	      }
	    else
	      return nullptr;

	  if (auto val = m_vp->next ())
	    {
	      auto ret = std::make_unique <valfile> (*m_vf);
	      ret->push (std::move (val));
	      return ret;
	    }

	  reset_me ();
	}
    }

    std::string
    name () const override
    {
      return m_name;
    }

    void
    reset ()
    {
      reset_me ();
      inner_op::reset ();
    }
  };

  template <std::unique_ptr <value_producer> F (value_loclist_op &,
						dwgrep_graph::sptr),
	    char const *Name>
  struct loclist_op_builtin
    : public builtin
  {
    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <op_loclist_op> (upstream, q, F, Name);
    }

    char const *
    name () const override
    {
      return Name;
    }
  };

  constexpr char const at_number_name[] = "@number";

  std::unique_ptr <value_producer>
  loclist_operate_number (value_loclist_op &v, dwgrep_graph::sptr gr)
  {
    return dwop_number (v.get_dwop (), v.get_attr (), gr);
  }

  typedef loclist_op_builtin <loclist_operate_number,
			      at_number_name> builtin_at_number;


  constexpr char const at_number2_name[] = "@number2";

  std::unique_ptr <value_producer>
  loclist_operate_number2 (value_loclist_op &v, dwgrep_graph::sptr gr)
  {
    return dwop_number2 (v.get_dwop (), v.get_attr (), gr);
  }

  typedef loclist_op_builtin <loclist_operate_number2,
			      at_number2_name> builtin_at_number2;
}

namespace
{
  struct builtin_rootp
    : public pred_builtin
  {
    using pred_builtin::pred_builtin;

    struct p
      : public pred
    {
      dwgrep_graph::sptr m_g;

      explicit p (dwgrep_graph::sptr g)
	: m_g {g}
      {}

      pred_result
      result (valfile &vf) override
      {
	if (auto v = vf.top_as <value_die> ())
	  return pred_result (m_g->is_root (v->get_die ()));

	else if (vf.top ().is <value_attr> ())
	  // By definition, attributes are never root.
	  return pred_result::no;

	else
	  {
	    show_expects (name (), {value_die::vtype, value_attr::vtype});
	    return pred_result::fail;
	  }
      }

      void
      reset () override
      {}

      std::string
      name () const override
      {
	return "?root";
      }
    };

    std::unique_ptr <pred>
    build_pred (dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return maybe_invert (std::make_unique <p> (q));
    }

    char const *
    name () const override
    {
      if (m_positive)
	return "?root";
      else
	return "!root";
    }
  };
}

namespace
{
  struct builtin_value_attr
    : public builtin
  {
    struct o
      : public inner_op
    {
      dwgrep_graph::sptr m_gr;
      std::unique_ptr <value_producer> m_vpr;
      valfile::uptr m_vf;

      o (std::shared_ptr <op> m_upstream, dwgrep_graph::sptr gr)
	: inner_op {m_upstream}
	, m_gr {gr}
      {}

      void
      reset_me ()
      {
	m_vf = nullptr;
	m_vpr = nullptr;
      }

      valfile::uptr
      next () override
      {
	while (true)
	  {
	    while (m_vpr == nullptr)
	      if (m_vf = m_upstream->next ())
		{
		  auto vp = m_vf->pop_as <value_attr> ();
		  m_vpr = at_value (vp->get_attr (), vp->get_die (), m_gr);
		}
	      else
		return nullptr;

	    if (auto v = m_vpr->next ())
	      {
		auto vf = std::make_unique <valfile> (*m_vf);
		vf->push (std::move (v));
		return vf;
	      }

	    reset_me ();
	  }
      }

      void
      reset () override
      {
	reset_me ();
	m_upstream->reset ();
      }

      std::string
      name () const
      {
	return "value";
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return std::make_shared <o> (upstream, q);
    }

    char const *
    name () const override final
    {
      return "overload";
    }
  };
}

namespace
{
  struct builtin_attr_named
    : public builtin
  {
    int m_atname;
    explicit builtin_attr_named (int atname)
      : m_atname {atname}
    {}

    struct o
      : public dwop_f
    {
      int m_atname;

    public:
      o (std::shared_ptr <op> upstream, dwgrep_graph::sptr g, int atname)
	: dwop_f {upstream, g}
	, m_atname {atname}
      {}

      bool
      operate (valfile &vf, Dwarf_Die &die) override
      {
	Dwarf_Attribute attr;
	if (dwarf_attr (&die, m_atname, &attr) == nullptr)
	  return false;

	vf.push (std::make_unique <value_attr> (m_g, attr, die, 0));
	return true;
      }

      std::string
      name () const override
      {
	std::stringstream ss;
	ss << "@" << constant {m_atname, &dw_attr_dom};
	return ss.str ();
      }
    };

    std::shared_ptr <op>
    build_exec (std::shared_ptr <op> upstream, dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      auto t = std::make_shared <o> (upstream, q, m_atname);
      return std::make_shared <builtin_value_attr::o> (t, q);
    }

    char const *
    name () const override
    {
      return "@attr";
    }
  };
}

namespace
{
  struct builtin_pred_attr
    : public pred_builtin
  {
    int m_atname;
    builtin_pred_attr (int atname, bool positive)
      : pred_builtin {positive}
      , m_atname {atname}
    {}

    struct p
      : public pred
    {
      unsigned m_atname;
      constant m_const;

      explicit p (unsigned atname)
	: m_atname {atname}
	, m_const {atname, &dw_attr_dom}
      {}

      pred_result
      result (valfile &vf) override
      {
	if (auto v = vf.top_as <value_die> ())
	  {
	    Dwarf_Die *die = &v->get_die ();
	    return pred_result (dwarf_hasattr (die, m_atname) != 0);
	  }

	else if (auto v = vf.top_as <value_cst> ())
	  {
	    check_constants_comparable (m_const, v->get_constant ());
	    return pred_result (m_const == v->get_constant ());
	  }

	else if (auto v = vf.top_as <value_attr> ())
	  return pred_result (dwarf_whatattr (&v->get_attr ()) == m_atname);

	else
	  {
	    show_expects (name (),
			  {value_die::vtype,
			   value_attr::vtype, value_cst::vtype});
	    return pred_result::fail;
	  }
      }

      void reset () override {}

      std::string
      name () const override
      {
	std::stringstream ss;
	ss << "?AT_" << constant {m_atname, &dw_attr_short_dom};
	return ss.str ();
      }
    };

    std::unique_ptr <pred>
    build_pred (dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return maybe_invert (std::make_unique <p> (m_atname));
    }

    char const *
    name () const override
    {
      if (m_positive)
	return "?attr";
      else
	return "!attr";
    }
  };
}

namespace
{
  struct builtin_pred_tag
    : public pred_builtin
  {
    int m_tag;
    builtin_pred_tag (int tag, bool positive)
      : pred_builtin {positive}
      , m_tag {tag}
    {}

    struct p
      : public pred
    {
      int m_tag;
      constant m_const;

      explicit p (int tag)
	: m_tag {tag}
	, m_const {(unsigned) tag, &dw_tag_dom}
      {}

      pred_result
      result (valfile &vf) override
      {
	if (auto v = vf.top_as <value_die> ())
	  return pred_result (dwarf_tag (&v->get_die ()) == m_tag);

	else if (auto v = vf.top_as <value_cst> ())
	  {
	    check_constants_comparable (m_const, v->get_constant ());
	    return pred_result (m_const == v->get_constant ());
	  }

	else
	  {
	    show_expects (name (), {value_die::vtype, value_cst::vtype});
	    return pred_result::fail;
	  }
      }

      void reset () override {}

      std::string
      name () const override
      {
	std::stringstream ss;
	ss << "?" << m_const;
	return ss.str ();
      }
    };

    std::unique_ptr <pred>
    build_pred (dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return maybe_invert (std::make_unique <p> (m_tag));
    }

    char const *
    name () const override
    {
      if (m_positive)
	return "?tag";
      else
	return "!tag";
    }
  };
}

namespace
{
  struct builtin_pred_form
    : public pred_builtin
  {
    unsigned m_form;
    builtin_pred_form (unsigned form, bool positive)
      : pred_builtin {positive}
      , m_form {form}
    {}

    struct p
      : public pred
    {
      unsigned m_form;
      constant m_const;

      explicit p (unsigned form)
	: m_form {form}
	, m_const {m_form, &dw_form_dom}
      {}

      pred_result
      result (valfile &vf) override
      {
	if (auto v = vf.top_as <value_attr> ())
	  return pred_result (dwarf_whatform (&v->get_attr ()) == m_form);

	else if (auto v = vf.top_as <value_cst> ())
	  {
	    check_constants_comparable (m_const, v->get_constant ());
	    return pred_result (m_const == v->get_constant ());
	  }

	else
	  {
	    show_expects (name (), {value_attr::vtype, value_cst::vtype});
	    return pred_result::fail;
	  }
      }

      void reset () override {}

      std::string
      name () const override
      {
	std::stringstream ss;
	ss << "?" << m_const;
	return ss.str ();
      }
    };

    std::unique_ptr <pred>
    build_pred (dwgrep_graph::sptr q,
		std::shared_ptr <scope> scope) const override
    {
      return maybe_invert (std::make_unique <p> (m_form));
    }

    char const *
    name () const override
    {
      if (m_positive)
	return "?form";
      else
	return "!form";
    }
  };
}

std::unique_ptr <builtin_dict>
dwgrep_builtins_dw ()
{
  auto ret = std::make_unique <builtin_dict> ();
  builtin_dict &dict = *ret;

  add_builtin_type_constant <value_die> (dict);
  add_builtin_type_constant <value_attr> (dict);
  add_builtin_type_constant <value_loclist_op> (dict);

  dict.add (std::make_shared <builtin_winfo> ());
  dict.add (std::make_shared <builtin_unit> ());

  dict.add (std::make_shared <builtin_child> ());
  dict.add (std::make_shared <builtin_attribute> ());
  dict.add (std::make_shared <builtin_offset> ());
  dict.add (std::make_shared <builtin_label> ());
  dict.add (std::make_shared <builtin_form> ());
  dict.add (std::make_shared <builtin_parent> ());
  dict.add (std::make_shared <builtin_integrate> ());
  dict.add (std::make_shared <builtin_at_number> ());
  dict.add (std::make_shared <builtin_at_number2> ());

  dict.add (std::make_shared <builtin_rootp> (true));
  dict.add (std::make_shared <builtin_rootp> (false));

#define ONE_KNOWN_DW_AT(NAME, CODE)					\
  {									\
    auto b1 = std::make_shared <builtin_attr_named> (CODE);		\
    dict.add (b1, "@AT_" #NAME);					\
    dict.add (b1, "@" #CODE);						\
    auto b2 = std::make_shared <builtin_pred_attr> (CODE, true);	\
    auto nb2 = std::make_shared <builtin_pred_attr> (CODE, false);	\
    dict.add (b2, "?AT_" #NAME);					\
    dict.add (nb2, "!AT_" #NAME);					\
    dict.add (b2, "?" #CODE);						\
    dict.add (nb2, "!" #CODE);						\
    add_builtin_constant (dict, constant (CODE, &dw_attr_dom), #CODE);	\
  }
  ALL_KNOWN_DW_AT;
#undef ONE_KNOWN_DW_AT

#define ONE_KNOWN_DW_TAG(NAME, CODE)					\
  {									\
    auto b1 = std::make_shared <builtin_pred_tag> (CODE, true);		\
    auto nb1 = std::make_shared <builtin_pred_tag> (CODE, false);	\
    dict.add (b1, "?TAG_" #NAME);					\
    dict.add (nb1, "!TAG_" #NAME);					\
    dict.add (b1, "?" #CODE);						\
    dict.add (nb1, "!" #CODE);						\
    add_builtin_constant (dict, constant (CODE, &dw_tag_dom), #CODE);	\
  }
  ALL_KNOWN_DW_TAG;
#undef ONE_KNOWN_DW_TAG

#define ONE_KNOWN_DW_FORM_DESC(NAME, CODE, DESC) ONE_KNOWN_DW_FORM (NAME, CODE)
#define ONE_KNOWN_DW_FORM(NAME, CODE)					\
  {									\
    auto b1 = std::make_shared <builtin_pred_form> (CODE, true);	\
    auto nb1 = std::make_shared <builtin_pred_form> (CODE, false);	\
    dict.add (b1, "?FORM_" #NAME);					\
    dict.add (nb1, "!FORM_" #NAME);					\
    dict.add (b1, "?" #CODE);						\
    dict.add (nb1, "!" #CODE);						\
    add_builtin_constant (dict, constant (CODE, &dw_form_dom), #CODE);	\
  }
  ALL_KNOWN_DW_FORM;
#undef ONE_KNOWN_DW_FORM
#undef ONE_KNOWN_DW_FORM_DESC

#define ONE_KNOWN_DW_LANG_DESC(NAME, CODE, DESC)			\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_lang_dom), #CODE);	\
  }
  ALL_KNOWN_DW_LANG;
#undef ONE_KNOWN_DW_LANG_DESC

#define ONE_KNOWN_DW_MACINFO(NAME, CODE)				\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_macinfo_dom), #CODE); \
  }
  ALL_KNOWN_DW_MACINFO;
#undef ONE_KNOWN_DW_MACINFO

#define ONE_KNOWN_DW_MACRO_GNU(NAME, CODE)				\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_macro_dom), #CODE); \
  }
  ALL_KNOWN_DW_MACRO_GNU;
#undef ONE_KNOWN_DW_MACRO_GNU

#define ONE_KNOWN_DW_INL(NAME, CODE)					\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_inline_dom), #CODE); \
  }
  ALL_KNOWN_DW_INL;
#undef ONE_KNOWN_DW_INL

#define ONE_KNOWN_DW_ATE(NAME, CODE)					\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_encoding_dom), #CODE); \
  }
  ALL_KNOWN_DW_ATE;
#undef ONE_KNOWN_DW_ATE

#define ONE_KNOWN_DW_ACCESS(NAME, CODE)					\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_access_dom), #CODE); \
  }
  ALL_KNOWN_DW_ACCESS;
#undef ONE_KNOWN_DW_ACCESS

#define ONE_KNOWN_DW_VIS(NAME, CODE)					\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_visibility_dom), #CODE); \
  }
  ALL_KNOWN_DW_VIS;
#undef ONE_KNOWN_DW_VIS

#define ONE_KNOWN_DW_VIRTUALITY(NAME, CODE)				\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_virtuality_dom), #CODE); \
  }
  ALL_KNOWN_DW_VIRTUALITY;
#undef ONE_KNOWN_DW_VIRTUALITY

#define ONE_KNOWN_DW_ID(NAME, CODE)					\
  {									\
    add_builtin_constant (dict,						\
			  constant (CODE, &dw_identifier_case_dom), #CODE); \
  }
  ALL_KNOWN_DW_ID;
#undef ONE_KNOWN_DW_ID

#define ONE_KNOWN_DW_CC(NAME, CODE)					\
  {									\
    add_builtin_constant (dict,						\
			  constant (CODE, &dw_calling_convention_dom), #CODE); \
  }
  ALL_KNOWN_DW_CC;
#undef ONE_KNOWN_DW_CC

#define ONE_KNOWN_DW_ORD(NAME, CODE)					\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_ordering_dom), #CODE); \
  }
  ALL_KNOWN_DW_ORD;
#undef ONE_KNOWN_DW_ORD

#define ONE_KNOWN_DW_DSC(NAME, CODE)					\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_discr_list_dom), #CODE); \
  }
  ALL_KNOWN_DW_DSC;
#undef ONE_KNOWN_DW_DSC

#define ONE_KNOWN_DW_DS(NAME, CODE)					\
  {									\
    add_builtin_constant (dict,						\
			  constant (CODE, &dw_decimal_sign_dom), #CODE); \
  }
  ALL_KNOWN_DW_DS;
#undef ONE_KNOWN_DW_DS

#define ONE_KNOWN_DW_OP_DESC(NAME, CODE, DESC) ONE_KNOWN_DW_OP (NAME, CODE)
#define ONE_KNOWN_DW_OP(NAME, CODE)					\
  {									\
    add_builtin_constant (dict,						\
			  constant (CODE, &dw_locexpr_opcode_dom), #CODE); \
  }
  ALL_KNOWN_DW_OP;
#undef ONE_KNOWN_DW_OP
#undef ONE_KNOWN_DW_OP_DESC

  add_builtin_constant (dict, constant (DW_ADDR_none, &dw_address_class_dom),
			"DW_ADDR_none");

#define ONE_KNOWN_DW_END(NAME, CODE)					\
  {									\
    add_builtin_constant (dict, constant (CODE, &dw_endianity_dom), #CODE); \
  }
  ALL_KNOWN_DW_END;
#undef ONE_KNOWN_DW_END

  {
    auto t = std::make_shared <overload_tab> ();

    t->add_overload (value_attr::vtype,
		     std::make_shared <builtin_value_attr> ());

    dict.add (std::make_shared <overloaded_op_builtin> ("value", t));
  }

  return ret;
}
