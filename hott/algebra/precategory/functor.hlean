/-
Copyright (c) 2015 Floris van Doorn. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Module: algebra.precategory.functor
Authors: Floris van Doorn, Jakob von Raumer
-/

import .basic types.pi .iso

open function category eq prod equiv is_equiv sigma sigma.ops is_trunc funext iso
open pi

structure functor (C D : Precategory) : Type :=
  (to_fun_ob : C → D)
  (to_fun_hom : Π ⦃a b : C⦄, hom a b → hom (to_fun_ob a) (to_fun_ob b))
  (respect_id : Π (a : C), to_fun_hom (ID a) = ID (to_fun_ob a))
  (respect_comp : Π {a b c : C} (g : hom b c) (f : hom a b),
    to_fun_hom (g ∘ f) = to_fun_hom g ∘ to_fun_hom f)

namespace functor

  infixl `⇒`:25 := functor
  variables {A B C D E : Precategory}

  attribute to_fun_ob [coercion]
  attribute to_fun_hom [coercion]

  -- The following lemmas will later be used to prove that the type of
  -- precategories forms a precategory itself
  protected definition compose [reducible] (G : functor D E) (F : functor C D) : functor C E :=
  functor.mk
    (λ x, G (F x))
    (λ a b f, G (F f))
    (λ a, calc
      G (F (ID a)) = G (ID (F a)) : by rewrite respect_id
               ... = ID (G (F a)) : by rewrite respect_id)
    (λ a b c g f, calc
      G (F (g ∘ f)) = G (F g ∘ F f)     : by rewrite respect_comp
                ... = G (F g) ∘ G (F f) : by rewrite respect_comp)

  infixr `∘f`:60 := compose

  protected definition id [reducible] {C : Precategory} : functor C C :=
  mk (λa, a) (λ a b f, f) (λ a, idp) (λ a b c f g, idp)

  protected definition ID [reducible] (C : Precategory) : functor C C := id

  definition functor_mk_eq' {F₁ F₂ : C → D} {H₁ : Π(a b : C), hom a b → hom (F₁ a) (F₁ b)}
    {H₂ : Π(a b : C), hom a b → hom (F₂ a) (F₂ b)} (id₁ id₂ comp₁ comp₂)
    (pF : F₁ = F₂) (pH : pF ▹ H₁ = H₂)
      : functor.mk F₁ H₁ id₁ comp₁ = functor.mk F₂ H₂ id₂ comp₂ :=
  apD01111 functor.mk pF pH !is_hprop.elim !is_hprop.elim

  definition functor_eq' {F₁ F₂ : C ⇒ D}
    : Π(p : to_fun_ob F₁ = to_fun_ob F₂),
          (transport (λx, Πa b f, hom (x a) (x b)) p (to_fun_hom F₁) = to_fun_hom F₂) → F₁ = F₂ :=
  functor.rec_on F₁ (λO₁ H₁ id₁ comp₁, functor.rec_on F₂ (λO₂ H₂ id₂ comp₂ p, !functor_mk_eq'))

  definition functor_mk_eq {F₁ F₂ : C → D} {H₁ : Π(a b : C), hom a b → hom (F₁ a) (F₁ b)}
    {H₂ : Π(a b : C), hom a b → hom (F₂ a) (F₂ b)} (id₁ id₂ comp₁ comp₂) (pF : F₁ ∼ F₂)
    (pH : Π(a b : C) (f : hom a b), hom_of_eq (pF b) ∘ H₁ a b f ∘ inv_of_eq (pF a) = H₂ a b f)
      : functor.mk F₁ H₁ id₁ comp₁ = functor.mk F₂ H₂ id₂ comp₂ :=
  functor_mk_eq' id₁ id₂ comp₁ comp₂ (eq_of_homotopy pF)
    (eq_of_homotopy (λc, eq_of_homotopy (λc', eq_of_homotopy (λf,
      begin
        apply concat, rotate_left 1, exact (pH c c' f),
        apply concat, rotate_left 1, apply transport_hom,
        apply concat, rotate_left 1,
        exact (pi_transport_constant (eq_of_homotopy pF) (H₁ c c') f),
        apply (apD10' f),
        apply concat, rotate_left 1,
        exact (pi_transport_constant (eq_of_homotopy pF) (H₁ c) c'),
        apply (apD10' c'),
        apply concat, rotate_left 1,
        exact (pi_transport_constant (eq_of_homotopy pF) H₁ c),
        apply idp
      end))))

  definition functor_eq {F₁ F₂ : C ⇒ D} : Π(p : to_fun_ob F₁ ∼ to_fun_ob F₂),
    (Π(a b : C) (f : hom a b), hom_of_eq (p b) ∘ F₁ f ∘ inv_of_eq (p a) = F₂ f) → F₁ = F₂ :=
  functor.rec_on F₁ (λO₁ H₁ id₁ comp₁, functor.rec_on F₂ (λO₂ H₂ id₂ comp₂ p, !functor_mk_eq))

  definition functor_mk_eq_constant {F : C → D} {H₁ : Π(a b : C), hom a b → hom (F a) (F b)}
    {H₂ : Π(a b : C), hom a b → hom (F a) (F b)} (id₁ id₂ comp₁ comp₂)
    (pH : Π(a b : C) (f : hom a b), H₁ a b f = H₂ a b f)
      : functor.mk F H₁ id₁ comp₁ = functor.mk F H₂ id₂ comp₂ :=
  functor_eq (λc, idp) (λa b f, !id_leftright ⬝ !pH)

  protected definition preserve_iso (F : C ⇒ D) {a b : C} (f : hom a b) [H : is_iso f] :
    is_iso (F f) :=
  begin
    fapply @is_iso.mk, apply (F (f⁻¹)),
    repeat (apply concat ; apply inverse ;  apply (respect_comp F) ;
      apply concat ; apply (ap (λ x, to_fun_hom F x)) ;
      [apply left_inverse | apply right_inverse] ;
      apply (respect_id F) ),
  end

  attribute preserve_iso [instance]

  protected definition respect_inv (F : C ⇒ D) {a b : C} (f : hom a b)
    [H : is_iso f] [H' : is_iso (F f)] :
    F (f⁻¹) = (F f)⁻¹ :=
  begin
    fapply @left_inverse_eq_right_inverse, apply (F f),
      apply concat, apply inverse, apply (respect_comp F),
      apply concat, apply (ap (λ x, to_fun_hom F x)),
      apply left_inverse, apply respect_id,
    apply right_inverse,
  end

  protected definition assoc (H : C ⇒ D) (G : B ⇒ C) (F : A ⇒ B) :
      H ∘f (G ∘f F) = (H ∘f G) ∘f F :=
  !functor_mk_eq_constant (λa b f, idp)

  protected definition id_left  (F : C ⇒ D) : id ∘f F = F :=
  functor.rec_on F (λF1 F2 F3 F4, !functor_mk_eq_constant (λa b f, idp))

  protected definition id_right (F : C ⇒ D) : F ∘f id = F :=
  functor.rec_on F (λF1 F2 F3 F4, !functor_mk_eq_constant (λa b f, idp))

  protected definition comp_id_eq_id_comp (F : C ⇒ D) : F ∘f functor.id = functor.id ∘f F :=
  !functor.id_right ⬝ !functor.id_left⁻¹

  -- "functor C D" is equivalent to a certain sigma type
  protected definition sigma_char :
    (Σ (to_fun_ob : C → D)
    (to_fun_hom : Π ⦃a b : C⦄, hom a b → hom (to_fun_ob a) (to_fun_ob b)),
    (Π (a : C), to_fun_hom (ID a) = ID (to_fun_ob a)) ×
    (Π {a b c : C} (g : hom b c) (f : hom a b),
      to_fun_hom (g ∘ f) = to_fun_hom g ∘ to_fun_hom f)) ≃ (functor C D) :=
  begin
    fapply equiv.MK,
      {intro S, fapply functor.mk,
        exact (S.1), exact (S.2.1),
        exact (pr₁ S.2.2), exact (pr₂ S.2.2)},
      {intro F,
        cases F with [d1, d2, d3, d4],
        exact ⟨d1, d2, (d3, @d4)⟩},
      {intro F,
        cases F,
        apply idp},
      {intro S,
        cases S with [d1, S2],
        cases S2 with [d2, P1],
        cases P1,
        apply idp},
  end

  set_option apply.class_instance false
  protected definition is_hset_functor
    [HD : is_hset D] : is_hset (functor C D) :=
  begin
    apply is_trunc_is_equiv_closed, apply equiv.to_is_equiv,
      apply sigma_char,
    apply is_trunc_sigma, apply is_trunc_pi, intros, exact HD, intro F,
    apply is_trunc_sigma, apply is_trunc_pi, intro a,
      {apply is_trunc_pi, intro b,
       apply is_trunc_pi, intro c, apply !homH},
    intro H, apply is_trunc_prod,
      {apply is_trunc_pi, intro a,
       apply is_trunc_eq, apply is_trunc_succ, apply !homH},
      {repeat (apply is_trunc_pi; intros),
       apply is_trunc_eq, apply is_trunc_succ, apply !homH},
  end

  definition functor_mk_eq'_idp (F : C → D) (H : Π(a b : C), hom a b → hom (F a) (F b))
    (id comp) : functor_mk_eq' id id comp comp (idpath F) (idpath H) = idp :=
  begin
    fapply (apD011 (apD01111 functor.mk idp idp)),
    apply is_hset.elim,
    apply is_hset.elim
  end

  definition functor_eq'_idp (F : C ⇒ D) : functor_eq' idp idp = (idpath F) :=
  by (cases F; apply functor_mk_eq'_idp)

  definition functor_eq_eta' {F₁ F₂ : C ⇒ D} (p : F₁ = F₂)
      : functor_eq' (ap to_fun_ob p) (!transport_compose⁻¹ ⬝ apD to_fun_hom p) = p :=
  begin
    cases p, cases F₁,
    apply concat, rotate_left 1, apply functor_eq'_idp,
    apply (ap (functor_eq' idp)),
    apply idp_con,
  end

  definition functor_eq2' {F₁ F₂ : C ⇒ D} {p₁ p₂ : to_fun_ob F₁ = to_fun_ob F₂} (q₁ q₂)
    (r : p₁ = p₂) : functor_eq' p₁ q₁ = functor_eq' p₂ q₂ :=
  by cases r; apply (ap (functor_eq' p₂)); apply is_hprop.elim

  definition functor_eq2 {F₁ F₂ : C ⇒ D} (p q : F₁ = F₂) (r : ap010 to_fun_ob p ∼ ap010 to_fun_ob q)
    : p = q :=
  begin
    cases F₁ with [ob₁, hom₁, id₁, comp₁],
    cases F₂ with [ob₂, hom₂, id₂, comp₂],
    rewrite [-functor_eq_eta' p, -functor_eq_eta' q],
    apply functor_eq2',
    apply ap_eq_ap_of_homotopy,
    exact r,
  end

  -- definition ap010_functor_eq_mk' {F₁ F₂ : C ⇒ D} (p : to_fun_ob F₁ = to_fun_ob F₂)
  --   (q : p ▹ F₁ = F₂) (c : C) :
  --   ap to_fun_ob (functor_eq_mk (apD10 p) (λa b f, _)) = p := sorry
  -- begin
  --   cases F₂, revert q, apply (homotopy.rec_on p), clear p, esimp, intros (p, q),
  --   cases p, clears (e_1, e_2),
  -- end

  -- TODO: remove sorry
  definition ap010_functor_eq {F₁ F₂ : C ⇒ D} (p : to_fun_ob F₁ ∼ to_fun_ob F₂)
    (q : (λ(a b : C) (f : hom a b), hom_of_eq (p b) ∘ F₁ f ∘ inv_of_eq (p a)) ∼3 to_fun_hom F₂) (c : C) :
    ap010 to_fun_ob (functor_eq p q) c = p c :=
  begin
    cases F₂, revert q, apply (homotopy.rec_on p), clear p, esimp, intros [p, q],
    apply sorry,
    --apply (homotopy3.rec_on q), clear q, intro q,
    --cases p, --TODO: report: this fails
  end

  definition ap010_functor_mk_eq_constant {F : C → D} {H₁ : Π(a b : C), hom a b → hom (F a) (F b)}
    {H₂ : Π(a b : C), hom a b → hom (F a) (F b)} {id₁ id₂ comp₁ comp₂}
    (pH : Π(a b : C) (f : hom a b), H₁ a b f = H₂ a b f) (c : C) :
  ap010 to_fun_ob (functor_mk_eq_constant id₁ id₂ comp₁ comp₂ pH) c = idp :=
  !ap010_functor_eq

  --do we need this theorem?
  definition compose_pentagon (K : D ⇒ E) (H : C ⇒ D) (G : B ⇒ C) (F : A ⇒ B) :
    (calc K ∘f H ∘f G ∘f F = (K ∘f H) ∘f G ∘f F : functor.assoc
      ... = ((K ∘f H) ∘f G) ∘f F : functor.assoc)
    =
    (calc K ∘f H ∘f G ∘f F = K ∘f (H ∘f G) ∘f F : ap (λx, K ∘f x) !functor.assoc
      ... = (K ∘f H ∘f G) ∘f F : functor.assoc
      ... = ((K ∘f H) ∘f G) ∘f F : ap (λx, x ∘f F) !functor.assoc) :=
  sorry
  -- begin
  -- apply functor_eq2,
  -- intro a,
  -- rewrite +ap010_con,
  -- -- rewrite +ap010_ap,
  -- -- apply sorry
  -- /-to prove this we need a stronger ap010-lemma, something like
  --     ap010 (λy, to_fun_ob (f y)) (functor_mk_eq_constant ...) c = idp
  --   or something another way of getting ap out of ap010
  -- -/
  -- --rewrite +ap010_ap,
  -- --unfold functor.assoc,
  -- --rewrite ap010_functor_mk_eq_constant,
  -- end

end functor
