# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# Contributor(s):
#   Chris Jones <jones.chris.g@gmail.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either of the GNU General Public License Version 2 or later (the "GPL"),
# or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

import copy, sys

class Visitor:
    def defaultVisit(self, node):
        raise Exception, "INTERNAL ERROR: no visitor for node type `%s'"% (
            node.__class__.__name__)

    def visitWhitespace(self, ws):
        pass

    def visitFile(self, f):
        for thing in f.stuff:
            thing.accept(self)

    def visitCppDirective(self, ppd):
        pass

    def visitBlock(self, block):
        for stmt in block.stmts:
            stmt.accept(self)

    def visitNamespace(self, ns):
        self.visitBlock(ns)

    def visitType(self, type):
        pass

    def visitTypeArray(self, ta):
        ta.basetype.accept(self)
        ta.nmemb.accept(self)

    def visitTypeEnum(self, enum):
        pass

    def visitTypeUnion(self, union):
        for t, name in union.components:
            t.accept(self)

    def visitTypedef(self, tdef):
        tdef.fromtype.accept(self)

    def visitForwardDecl(self, fd):
        pass

    def visitDecl(self, decl):
        decl.type.accept(self)

    def visitParam(self, param):
        self.visitDecl(param)
        if param.default is not None:
            param.default.accept(self)

    def visitClass(self, cls):
        for inherit in cls.inherits:
            inherit.accept(self)
        self.visitBlock(cls)

    def visitInherit(self, inh):
        pass

    def visitFriendClassDecl(self, fcd):
        pass

    def visitMethodDecl(self, meth):
        for param in meth.params:
            param.accept(self)
        if meth.ret is not None:
            meth.ret.accept(self)

    def visitMethodDefn(self, meth):
        meth.decl.accept(self)
        self.visitBlock(meth)

    def visitConstructorDecl(self, ctor):
        self.visitMethodDecl(ctor)

    def visitConstructorDefn(self, cd):
        cd.decl.accept(self)
        for init in cd.memberinits:
            init.accept(self)
        self.visitBlock(cd)

    def visitDestructorDecl(self, dtor):
        self.visitMethodDecl(dtor)

    def visitDestructorDefn(self, dd):
        dd.decl.accept(self)
        self.visitBlock(dd)

    def visitExprLiteral(self, l):
        pass

    def visitExprVar(self, v):
        pass

    def visitExprPrefixUnop(self, e):
        e.expr.accept(self)

    def visitExprBinary(self, e):
        e.left.accept(self)
        e.right.accept(self)

    def visitExprConditional(self, c):
        c.cond.accept(self)
        c.ife.accept(self)
        c.elsee.accept(self)

    def visitExprAddrOf(self, eao):
        self.visitExprPrefixUnop(eao)

    def visitExprDeref(self, ed):
        self.visitExprPrefixUnop(ed)

    def visitExprNot(self, en):
        self.visitExprPrefixUnop(en)

    def visitExprCast(self, ec):
        ec.expr.accept(self)

    def visitExprIndex(self, ei):
        ei.arr.accept(self)
        ei.idx.accept(self)

    def visitExprSelect(self, es):
        es.obj.accept(self)

    def visitExprAssn(self, ea):
        ea.lhs.accept(self)
        ea.rhs.accept(self)

    def visitExprCall(self, ec):
        ec.func.accept(self)
        for arg in ec.args:
            arg.accept(self)

    def visitExprNew(self, en):
        en.ctype.accept(self)
        if en.newargs is not None:
            for arg in en.newargs:
                arg.accept(self)
        if en.args is not None:
            for arg in en.args:
                arg.accept(self)

    def visitExprDelete(self, ed):
        ed.obj.accept(self)

    def visitExprMemberInit(self, minit):
        self.visitExprCall(minit)

    def visitExprSizeof(self, es):
        self.visitExprCall(es)

    def visitStmtBlock(self, sb):
        self.visitBlock(sb)

    def visitStmtDecl(self, sd):
        sd.decl.accept(self)
        if sd.init is not None:
            sd.init.accept(self)

    def visitLabel(self, label):
        pass

    def visitCaseLabel(self, case):
        pass

    def visitDefaultLabel(self, dl):
        pass

    def visitStmtIf(self, si):
        si.cond.accept(self)
        si.ifb.accept(self)
        if si.elseb is not None:
            si.elseb.accept(self)

    def visitStmtFor(self, sf):
        if sf.init is not None:
            sf.init.accept(self)
        if sf.cond is not None:
            sf.cond.accept(self)
        if sf.update is not None:
            sf.update.accept(self)

    def visitStmtSwitch(self, ss):
        ss.expr.accept(self)
        self.visitBlock(ss)

    def visitStmtBreak(self, sb):
        pass

    def visitStmtExpr(self, se):
        se.expr.accept(self)

    def visitStmtReturn(self, sr):
        if sr.expr is not None:
            sr.expr.accept(self)

##------------------------------
class Node:
    def __init__(self):
        pass

    def accept(self, visitor):
        visit = getattr(visitor, 'visit'+ self.__class__.__name__, None)
        if visit is None:
            return getattr(visitor, 'defaultVisit')(self)
        return visit(self)

class Whitespace(Node):
    # yes, this is silly.  but we need to stick comments in the
    # generated code without resorting to more serious hacks
    def __init__(self, ws, indent=0):
        Node.__init__(self)
        self.ws = ws
        self.indent = indent
Whitespace.NL = Whitespace('\n')

class File(Node):
    def __init__(self, filename):
        Node.__init__(self)
        self.name = filename
        # array of stuff in the file --- stmts and preprocessor thingies
        self.stuff = [ ]

    def addthing(self, thing):
        assert thing is not None
        assert not isinstance(thing, list)
        self.stuff.append(thing)

    def addthings(self, things):
        for t in things:  self.addthing(t)

    # "look like" a Block so code doesn't have to care whether they're
    # in global scope or not
    def addstmt(self, stmt):
        assert stmt is not None
        assert not isinstance(stmt, list)
        self.stuff.append(stmt)

    def addstmts(self, stmts):
        for s in stmts:  self.addstmt(s)

class CppDirective(Node):
    '''represents |#[directive] [rest]|, where |rest| is any string'''
    def __init__(self, directive, rest=''):
        Node.__init__(self)
        self.directive = directive
        self.rest = rest

class Block(Node):
    def __init__(self):
        Node.__init__(self)
        self.stmts = [ ]

    def addstmt(self, stmt):
        assert stmt is not None
        assert not isinstance(stmt, tuple)
        self.stmts.append(stmt)

    def addstmts(self, stmts):
        for s in stmts:  self.addstmt(s)

##------------------------------
# type and decl thingies
class Namespace(Block):
    def __init__(self, name):
        assert isinstance(name, str)

        Block.__init__(self)
        self.name = name

class Type(Node):
    def __init__(self, name, const=0,
                 ptr=0, ptrconst=0, ptrptr=0, ptrconstptr=0,
                 ref=0,
                 T=None):
        """
To avoid getting fancy with recursive types, we limit the kinds
of pointer types that can be be constructed.

  ptr            => T*
  ptrconst       => T* const
  ptrptr         => T**
  ptrconstptr    => T* const*

Any type, naked or pointer, can be const (const T) or ref (T&).
"""
        assert isinstance(name, str)
        assert not isinstance(const, str)
        assert not isinstance(T, str)

        Node.__init__(self)
        self.name = name
        self.const = const
        self.ptr = ptr
        self.ptrconst = ptrconst
        self.ptrptr = ptrptr
        self.ptrconstptr = ptrconstptr
        self.ref = ref
        self.T = T
        # XXX could get serious here with recursive types, but shouldn't 
        # need that for this codegen
    def __deepcopy__(self, memo):
        return Type(self.name,
                    const=self.const,
                    ptr=self.ptr, ptrconst=self.ptrconst,
                    ptrptr=self.ptrptr, ptrconstptr=self.ptrconstptr,
                    ref=self.ref,
                    T=copy.deepcopy(self.T, memo))
Type.BOOL = Type('bool')
Type.INT = Type('int')
Type.INT32 = Type('int32')
Type.INTPTR = Type('intptr_t')
Type.UINT32 = Type('uint32')
Type.SIZE = Type('size_t')
Type.VOID = Type('void')
Type.VOIDPTR = Type('void', ptr=1)

class TypeArray(Node):
    def __init__(self, basetype, nmemb):
        '''the type |basetype DECLNAME[nmemb]|.  |nmemb| is an Expr'''
        self.basetype = basetype
        self.nmemb = nmemb
    def __deepcopy__(self, memo):
        return TypeArray(deepcopy(self.basetype, memo), nmemb)

class TypeEnum(Node):
    def __init__(self, name=None):
        '''name can be None'''
        Node.__init__(self)
        self.name = name
        self.idnums = [ ]    # pairs of ('Foo', [num]) or ('Foo', None)

    def addId(self, id, num=None):
        self.idnums.append((id, num))

class TypeUnion(Node):
    def __init__(self, name=None):
        Node.__init__(self)
        self.name = name
        self.components = [ ]           # [ Decl ]

    def addComponent(self, type, name):
        self.components.append(Decl(type, name))

class Typedef(Node):
    def __init__(self, fromtype, totypename):
        assert isinstance(totypename, str)
        
        Node.__init__(self)
        self.fromtype = fromtype
        self.totypename = totypename

class ForwardDecl(Node):
    def __init__(self, pqname, cls=0, struct=0):
        assert (not cls and struct) or (cls and not struct)

        self.pqname = pqname
        self.cls = cls
        self.struct = struct

class Decl(Node):
    '''represents |Foo bar|, e.g. in a function signature'''
    def __init__(self, type, name):
        assert type is not None
        assert not isinstance(type, str)
        assert isinstance(name, str)

        Node.__init__(self)
        self.type = type
        self.name = name
    def __deepcopy__(self, memo):
        return Decl(copy.deepcopy(self.type, memo), self.name)

class Param(Decl):
    def __init__(self, type, name, default=None):
        Decl.__init__(self, type, name)
        self.default = default
    def __deepcopy__(self, memo):
        return Param(copy.deepcopy(self.type, memo), self.name,
                     copy.deepcopy(self.default, memo))

##------------------------------
# class stuff
class Class(Block):
    def __init__(self, name, inherits=[ ],
                 interface=0, abstract=0, final=0,
                 specializes=None, struct=0):
        assert not (interface and abstract)
        assert not (abstract and final)
        assert not (interface and final)
        assert not (inherits and specializes)

        Block.__init__(self)
        self.name = name
        self.inherits = inherits        # [ Type ]
        self.interface = interface      # bool
        self.abstract = abstract        # bool
        self.final = final              # bool
        self.specializes = specializes  # Type or None
        self.struct = struct            # bool

class Inherit(Node):
    def __init__(self, type, viz='public'):
        assert isinstance(viz, str)
        Node.__init__(self)
        self.type = type
        self.viz = viz

class FriendClassDecl(Node):
    def __init__(self, friend):
        Node.__init__(self)
        self.friend = friend

class MethodDecl(Node):
    def __init__(self, name, params=[ ], ret=Type('void'),
                 virtual=0, const=0, pure=0, static=0,
                 typeop=None):
        assert not (virtual and static)
        assert not pure or virtual      # pure => virtual
        assert not (static and typeop)
        assert not (name and typeop)
        assert name is None or isinstance(name, str)
        assert not isinstance(ret, list)

        if typeop is not None:
            ret = None

        Node.__init__(self)
        self.name = name
        self.params = params            # [ Param ]
        self.ret = ret                  # Type or None
        self.virtual = virtual          # bool
        self.const = const              # bool
        self.pure = pure                # bool
        self.static = static            # bool
        self.typeop = typeop            # Type or None

    def __deepcopy__(self, memo):
        return MethodDecl(
            self.name,
            copy.deepcopy(self.params, memo),
            copy.deepcopy(self.ret, memo),
            self.virtual, self.const, self.pure, self.static,
            copy.deepcopy(self.typeop, memo))

class MethodDefn(Block):
    def __init__(self, decl):
        Block.__init__(self)
        self.decl = decl

class ConstructorDecl(MethodDecl):
    def __init__(self, name, params=[ ], explicit=0):
        MethodDecl.__init__(self, name, params=params, ret=None)
        self.explicit = explicit

    def __deepcopy__(self, memo):
        return ConstructorDecl(self.name,
                               copy.deepcopy(self.params, memo),
                               self.explicit)

class ConstructorDefn(MethodDefn):
    def __init__(self, decl, memberinits=[ ]):
        MethodDefn.__init__(self, decl)
        self.memberinits = memberinits

class DestructorDecl(MethodDecl):
    def __init__(self, name, virtual=0):
        MethodDecl.__init__(self, name, params=[ ], ret=None,
                            virtual=virtual)

    def __deepcopy__(self, memo):
        return DestructorDecl(self.name, self.virtual)

        
class DestructorDefn(MethodDefn):
    def __init__(self, decl):  MethodDefn.__init__(self, decl)

##------------------------------
# expressions
class ExprVar(Node):
    def __init__(self, name):
        assert isinstance(name, str)
        
        Node.__init__(self)
        self.name = name
ExprVar.THIS = ExprVar('this')

class ExprLiteral(Node):
    def __init__(self, value, type):
        '''|type| is a Python format specifier; 'd' for example'''
        Node.__init__(self)
        self.value = value
        self.type = type

    @staticmethod
    def Int(i):  return ExprLiteral(i, 'd')

    @staticmethod
    def String(s):  return ExprLiteral('"'+ s +'"', 's')

    @staticmethod
    def WString(s):  return ExprLiteral('L"'+ s +'"', 's')

    def __str__(self):
        return ('%'+ self.type)% (self.value)
ExprLiteral.ZERO = ExprLiteral.Int(0)
ExprLiteral.ONE = ExprLiteral.Int(1)
ExprLiteral.NULL = ExprLiteral.ZERO
ExprLiteral.TRUE = ExprVar('true')
ExprLiteral.FALSE = ExprVar('false')

class ExprPrefixUnop(Node):
    def __init__(self, expr, op):
        assert not isinstance(expr, tuple)
        self.expr = expr
        self.op = op

class ExprNot(ExprPrefixUnop):
    def __init__(self, expr):
        ExprPrefixUnop.__init__(self, expr, '!')

class ExprAddrOf(ExprPrefixUnop):
    def __init__(self, expr):
        ExprPrefixUnop.__init__(self, expr, '&')

class ExprDeref(ExprPrefixUnop):
    def __init__(self, expr):
        ExprPrefixUnop.__init__(self, expr, '*')

class ExprCast(Node):
    def __init__(self, expr, type,
                 dynamic=0, static=0, reinterpret=0, const=0, C=0):
        assert 1 == reduce(lambda a, x: a+x, [ dynamic, static, reinterpret, const, C ])

        Node.__init__(self)
        self.expr = expr
        self.type = type
        self.dynamic = dynamic
        self.static = static
        self.reinterpret = reinterpret
        self.const = const
        self.C = C

class ExprBinary(Node):
    def __init__(self, left, op, right):
        Node.__init__(self)
        self.left = left
        self.op = op
        self.right = right

class ExprConditional(Node):
    def __init__(self, cond, ife, elsee):
        Node.__init__(self)
        self.cond = cond
        self.ife = ife
        self.elsee = elsee

class ExprIndex(Node):
    def __init__(self, arr, idx):
        Node.__init__(self)
        self.arr = arr
        self.idx = idx

class ExprSelect(Node):
    def __init__(self, obj, op, field):
        assert obj and op and field
        assert not isinstance(obj, str)
        assert isinstance(field, str)
        
        Node.__init__(self)
        self.obj = obj
        self.op = op
        self.field = field

class ExprAssn(Node):
    def __init__(self, lhs, rhs, op='='):
        Node.__init__(self)
        self.lhs = lhs
        self.op = op
        self.rhs = rhs

class ExprCall(Node):
    def __init__(self, func, args=[ ]):
        assert hasattr(func, 'accept')
        assert isinstance(args, list)

        Node.__init__(self)
        self.func = func
        self.args = args

class ExprNew(Node):
    # XXX taking some poetic license ...
    def __init__(self, ctype, args=[ ], newargs=None):
        assert not (ctype.const or ctype.ref)

        Node.__init__(self)
        self.ctype = ctype
        self.args = args
        self.newargs = newargs

class ExprDelete(Node):
    def __init__(self, obj):
        Node.__init__(self)
        self.obj = obj

class ExprMemberInit(ExprCall):
    def __init__(self, member, args=[ ]):
        ExprCall.__init__(self, member, args)

class ExprSizeof(ExprCall):
    def __init__(self, t):
        ExprCall.__init__(self, ExprVar('sizeof'), [ t ])

##------------------------------
# statements etc.
class StmtBlock(Block):
    def __init__(self, stmts=[ ]):
        Block.__init__(self)
        self.addstmts(stmts)

class StmtDecl(Node):
    def __init__(self, decl, init=None, initargs=None):
        assert not (init and initargs)
        assert not isinstance(init, str) # easy to confuse with Decl
        assert not isinstance(decl, tuple)
        
        Node.__init__(self)
        self.decl = decl
        self.init = init
        self.initargs = initargs

class Label(Node):
    def __init__(self, name):
        Node.__init__(self)
        self.name = name
Label.PUBLIC = Label('public')
Label.PROTECTED = Label('protected')
Label.PRIVATE = Label('private')

class CaseLabel(Node):
    def __init__(self, name):
        Node.__init__(self)
        self.name = name

class DefaultLabel(Node):
    def __init__(self):
        Node.__init__(self)

class StmtIf(Node):
    def __init__(self, cond):
        Node.__init__(self)
        self.cond = cond
        self.ifb = Block()
        self.elseb = None

    def addifstmt(self, stmt):
        self.ifb.addstmt(stmt)

    def addifstmts(self, stmts):
        self.ifb.addstmts(stmts)
        
    def addelsestmt(self, stmt):
        if self.elseb is None: self.elseb = Block()
        self.elseb.addstmt(stmt)

    def addelsestmts(self, stmts):
        if self.elseb is None: self.elseb = Block()
        self.elseb.addstmts(stmts)

class StmtFor(Block):
    def __init__(self, init=None, cond=None, update=None):
        Block.__init__(self)
        self.init = init
        self.cond = cond
        self.update = update

class StmtSwitch(Block):
    def __init__(self, expr):
        Block.__init__(self)
        self.expr = expr
        self.nr_cases = 0

    def addcase(self, case, block):
        '''NOTE: |case| is not checked for uniqueness'''
        assert not isinstance(case, str)
        assert (isinstance(block, StmtBreak)
                or isinstance(block, StmtReturn)
                or (hasattr(block, 'stmts')
                    and (isinstance(block.stmts[-1], StmtBreak)
                         or isinstance(block.stmts[-1], StmtReturn))))
        self.addstmt(case)
        self.addstmt(block)
        self.nr_cases += 1

class StmtBreak(Node):
    def __init__(self):
        Node.__init__(self)

class StmtExpr(Node):
    def __init__(self, expr):
        Node.__init__(self)
        self.expr = expr

class StmtReturn(Node):
    def __init__(self, expr=None):
        Node.__init__(self)
        self.expr = expr
