#pragma once

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <iostream>


/** Copy-on-write shared ptr.
  * Allows to work with shared immutable objects and sometimes unshare and mutate you own unique copy.
  *
  * Usage:

    class Column : public COWPtr<Column>
    {
    private:
        friend class COWPtr<Column>;

        /// Leave all constructors in private section. They will be avaliable through 'create' method.
        Column();

        /// Provide 'clone' method. It can be virtual if you want polymorphic behaviour.
        virtual Column * clone() const;
    public:
        /// Correctly use const qualifiers in your interface.

        virtual ~IColumn() {}
    };

  * It will provide 'create' and 'mutate' methods.
  * And 'Ptr' and 'MutablePtr' types.
  * Ptr is refcounted pointer to immutable object.
  * MutablePtr is refcounted noncopyable pointer to mutable object.
  * MutablePtr can be assigned to Ptr through move assignment.
  *
  * 'create' method creates MutablePtr: you cannot share mutable objects.
  * To share, move-assign to immutable pointer.
  * 'mutate' method allows to create mutable noncopyable object from immutable object:
  *   either by cloning or by using directly, if it is not shared.
  * These methods are thread-safe.
  *
  * Example:
  *
    /// Creating and assigning to immutable ptr.
    Column::Ptr x = Column::create(1);
    /// Sharing single immutable object in two ptrs.
    Column::Ptr y = x;

    /// Now x and y are shared.

    /// Change value of x.
    {
        /// Creating mutable ptr. It can clone an object under the hood if it was shared.
        Column::MutablePtr mutate_x = x->mutate();
        /// Using non-const methods of an object.
        mutate_x->set(2);
        /// Assigning pointer 'x' to mutated object.
        x = std::move(mutate_x);
    }

    /// Now x and y are unshared and have different values.

  * Note. You may have heard that COW is bad practice.
  * Actually it is, if your values are small or if copying is done implicitly.
  * This is the case for string implementations.
  *
  * In contrast, COWPtr is intended for the cases when you need to share states of large objects,
  * (when you usually will use std::shared_ptr) but you also want precise control over modification
  * of this shared state.
  */
template <typename Derived>
class COWPtr : public boost::intrusive_ref_counter<Derived>
{
private:
    Derived * derived() { return static_cast<Derived *>(this); }
    const Derived * derived() const { return static_cast<const Derived *>(this); }

protected:
    template <typename T> class noncopyable : public T, private boost::noncopyable { using T::T; };

public:
    using MutablePtr = noncopyable<boost::intrusive_ptr<Derived>>;

protected:
    template <typename T> class immutable_ptr : public boost::intrusive_ptr<const T>
    {
    public:
        template <typename U>
        immutable_ptr(const boost::intrusive_ptr<const U> & other) : boost::intrusive_ptr<const T>(other) {}

        template <typename U>
        immutable_ptr(const boost::intrusive_ptr<U> && other) : boost::intrusive_ptr<const T>(std::move(other)) {}

        template <typename U>
        immutable_ptr(const boost::intrusive_ptr<U> &) = delete;
    };

public:
    using Ptr = immutable_ptr<Derived>;

    template <typename... Args>
    static MutablePtr create(Args &&... args) { return new Derived(std::forward<Args>(args)...); }

public:
    Ptr getPtr() const { return static_cast<Ptr>(derived()); }
    MutablePtr getPtr() { return static_cast<MutablePtr>(derived()); }

    MutablePtr mutate() const
    {
        if (this->use_count() > 1)
            return derived()->clone();
        else
            return const_cast<COWPtr*>(this)->getPtr();
    }
};


/** Helper class to support inheritance.
  * Example:
  *
  * class IColumn : public COWPtr<IColumn>
  * {
  *     friend class COWPtr<IColumn>;
  *     virtual IColumn * clone() const = 0;
  *     virtual ~IColumn() {}
  * };
  *
  * class ConcreteColumn : public COWPtrHelper<IColumn, ConcreteColumn>
  * {
  *     friend class COWPtrHelper<IColumn, ConcreteColumn>;
  * };
  *
  * Here is complete inheritance diagram:
  *
  * ConcreteColumn
  *  COWPtrHelper<IColumn, ConcreteColumn>
  *   IColumn
  *    CowPtr<IColumn>
  *     boost::intrusive_ref_counter<IColumn>
  */
template <typename Base, typename Derived>
class COWPtrHelper : public Base
{
private:
    Derived * derived() { return static_cast<Derived *>(this); }
    const Derived * derived() const { return static_cast<const Derived *>(this); }

public:
    using Ptr = typename Base::template immutable_ptr<Derived>;
    using MutablePtr = typename Base::template noncopyable<boost::intrusive_ptr<Derived>>;

    template <typename... Args>
    static MutablePtr create(Args &&... args) { return new Derived(std::forward<Args>(args)...); }

    Base * clone() const override { return new Derived(*derived()); }
};