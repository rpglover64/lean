-- Copyright (c) 2014 Microsoft Corporation. All rights reserved.
-- Released under Apache 2.0 license as described in the file LICENSE.
-- Author: Leonardo de Moura, Jeremy Avigad
import logic.inhabited logic.eq logic.decidable general_notation

-- data.prod
-- =========

open inhabited decidable eq.ops

structure prod (A B : Type) :=
mk :: (pr1 : A) (pr2 : B)

definition pair := @prod.mk

namespace prod
  notation A × B := prod A B

  -- notation for n-ary tuples
  notation `(` h `,` t:(foldl `,` (e r, prod.mk r e) h) `)` := t

  variables {A B : Type}
  protected theorem destruct {P : A × B → Prop} (p : A × B) (H : ∀a b, P (a, b)) : P p :=
  rec H p

  notation `pr₁` := pr1
  notation `pr₂` := pr2

  variables (a : A) (b : B)

  theorem pr1.pair : pr₁ (a, b) = a :=
  rfl

  theorem pr2.pair : pr₂ (a, b) = b :=
  rfl

  variables {a₁ a₂ : A} {b₁ b₂ : B}

  theorem pair_eq : a₁ = a₂ → b₁ = b₂ → (a₁, b₁) = (a₂, b₂) :=
  assume H1 H2, H1 ▸ H2 ▸ rfl

  protected theorem equal {p₁ p₂ : prod A B} : pr₁ p₁ = pr₁ p₂ → pr₂ p₁ = pr₂ p₂ → p₁ = p₂ :=
  destruct p₁ (take a₁ b₁, destruct p₂ (take a₂ b₂ H₁ H₂, pair_eq H₁ H₂))

  protected definition is_inhabited [instance] : inhabited A → inhabited B → inhabited (prod A B) :=
  take (H₁ : inhabited A) (H₂ : inhabited B),
    inhabited.destruct H₁ (λa, inhabited.destruct H₂ (λb, inhabited.mk (pair a b)))

  protected definition has_decidable_eq [instance] : decidable_eq A → decidable_eq B → decidable_eq (A × B) :=
  take (H₁ : decidable_eq A) (H₂ : decidable_eq B) (u v : A × B),
    have H₃ : u = v ↔ (pr₁ u = pr₁ v) ∧ (pr₂ u = pr₂ v), from
      iff.intro
        (assume H, H ▸ and.intro rfl rfl)
        (assume H, and.elim H (assume H₄ H₅, equal H₄ H₅)),
    decidable_iff_equiv _ (iff.symm H₃)
end prod
