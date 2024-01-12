// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/analyzer.h"

#include "StrictModules/Compiler/abstract_module_loader.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"
#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"
#include "StrictModules/exceptions.h"
#include "StrictModules/pycore_dependencies.h"

namespace strictmod {
using namespace objects;

// AnalysisContextManager
class LoopContinueException {};
class LoopBreakException {};

AnalysisContextManager::AnalysisContextManager(
    CallerContext& ctx,
    int newLine,
    int newCol)
    : context_(&ctx), oldLine_(ctx.lineno), oldCol_(ctx.col) {
  context_->lineno = newLine;
  context_->col = newCol;
}

AnalysisContextManager::~AnalysisContextManager() {
  context_->lineno = oldLine_;
  context_->col = oldCol_;
}

// Scope data
const AnalysisResult& AnalysisScopeData::getCallFirstArg() const {
  return callFirstArg_;
}

void AnalysisScopeData::set(const std::string& key, AnalysisResult value) {
  assert(prepareDict_ != nullptr);
  assert(caller_.has_value());
  iSetElement(prepareDict_, caller_->makeStr(key), value, *caller_);
}
AnalysisResult AnalysisScopeData::at(const std::string& key) {
  assert(prepareDict_ != nullptr);
  assert(caller_.has_value());
  return iGetElement(prepareDict_, caller_->makeStr(key), *caller_);
}

bool AnalysisScopeData::erase(const std::string& key) {
  assert(prepareDict_ != nullptr);
  assert(caller_.has_value());
  iDelElement(prepareDict_, caller_->makeStr(key), *caller_);
  return true;
}
bool AnalysisScopeData::contains(const std::string& key) const {
  assert(prepareDict_ != nullptr);
  assert(caller_.has_value());
  return iContainsElement(prepareDict_, caller_->makeStr(key), *caller_);
}

// template specialization for Scope with AnalysisScopeData
template <>
void Scope<AnalysisResult, AnalysisScopeData>::set(
    const std::string& key,
    AnalysisResult value) {
  if (data_.hasAlternativeDict()) {
    data_.set(key, std::move(value));
  } else {
    (*vars_)[key] = std::move(value);
  }
}

template <>
AnalysisResult Scope<AnalysisResult, AnalysisScopeData>::at(
    const std::string& key) {
  return data_.hasAlternativeDict() ? data_.at(key) : vars_->at(key);
}

template <>
bool Scope<AnalysisResult, AnalysisScopeData>::erase(const std::string& key) {
  if (data_.hasAlternativeDict()) {
    return data_.erase(key);
  }
  return vars_->erase(key) > 0;
}

template <>
bool Scope<AnalysisResult, AnalysisScopeData>::contains(
    const std::string& key) const {
  if (data_.hasAlternativeDict()) {
    return data_.contains(key);
  }
  return vars_->find(key) != vars_->map_end();
}

// Analyzer
Analyzer::Analyzer(
    mod_ty root,
    compiler::ModuleLoader* loader,
    Symtable table,
    BaseErrorSink* errors,
    std::string filename,
    std::string modName,
    std::string scopeName,
    std::shared_ptr<StrictModuleObject> caller,
    bool futureAnnotations)
    : Analyzer(
          root,
          loader,
          std::move(table),
          errors,
          std::move(filename),
          std::move(modName),
          std::move(scopeName),
          std::weak_ptr(caller),
          futureAnnotations) {}

Analyzer::Analyzer(
    mod_ty root,
    compiler::ModuleLoader* loader,
    Symtable table,
    BaseErrorSink* errors,
    std::string filename,
    std::string modName,
    std::string scopeName,
    std::weak_ptr<StrictModuleObject> caller,
    bool futureAnnotations)
    : root_(root),
      loader_(loader),
      context_(
          std::move(caller),
          std::move(filename),
          std::move(scopeName),
          0,
          0,
          errors,
          loader),
      stack_(
          table,
          scopeFactory,
          scopeFactory(table.entryFromAst(root_), getBuiltinsDict()),
          scopeFactory(
              table.entryFromAst(root_),
              std::make_shared<DictType>())),
      futureAnnotations_(futureAnnotations),
      currentExceptionContext_(),
      modName_(std::move(modName)),
      astToResults_(std::make_unique<astToResultT>()) {}

Analyzer::Analyzer(
    mod_ty root,
    compiler::ModuleLoader* loader,
    Symtable table,
    std::shared_ptr<DictType> toplevelNS,
    BaseErrorSink* errors,
    std::string filename,
    std::string modName,
    std::string scopeName,
    std::shared_ptr<StrictModuleObject> caller,
    bool futureAnnotations)
    : root_(root),
      loader_(loader),
      context_(
          std::move(caller),
          std::move(filename),
          std::move(scopeName),
          0,
          0,
          errors,
          loader),
      stack_(
          table,
          scopeFactory,
          scopeFactory(table.entryFromAst(root_), getBuiltinsDict()),
          scopeFactory(table.entryFromAst(root_), std::move(toplevelNS))),
      futureAnnotations_(futureAnnotations),
      currentExceptionContext_(),
      modName_(std::move(modName)),
      astToResults_(std::make_unique<astToResultT>()) {}

Analyzer::Analyzer(
    compiler::ModuleLoader* loader,
    BaseErrorSink* errors,
    std::string filename,
    std::string modName,
    std::string scopeName,
    std::weak_ptr<StrictModuleObject> caller,
    int lineno,
    int col,
    const EnvT& closure,
    bool futureAnnotations)
    : root_(nullptr),
      loader_(loader),
      context_(
          std::move(caller),
          std::move(filename),
          std::move(scopeName),
          lineno,
          col,
          errors,
          loader),
      stack_(EnvT(closure)),
      futureAnnotations_(futureAnnotations),
      currentExceptionContext_(),
      modName_(std::move(modName)),
      astToResults_(std::make_unique<astToResultT>()) {}

/* if asname is not nullptr, return asname,
 * otherwise return base name of the alias (substring before first '.')
 */
std::string importedNameHelper(alias_ty alias) {
  if (alias->asname != nullptr) {
    return PyUnicode_AsUTF8(alias->asname);
  } else {
    std::string name = PyUnicode_AsUTF8(alias->name);
    std::size_t it = name.find('.');
    return name.substr(0, it);
  }
}

std::shared_ptr<BaseStrictObject> Analyzer::handleFromListHelper(
    std::shared_ptr<BaseStrictObject> fromMod,
    const std::string& name) {
  auto value = iLoadAttr(fromMod, name, nullptr, context_);
  if (value != nullptr) {
    return value;
  }
  auto dunderPath = iLoadAttr(fromMod, "__path__", nullptr, context_);
  if (dunderPath == nullptr) {
    // not a package
    return nullptr;
  }
  auto modName = iLoadAttr(fromMod, "__name__", nullptr, context_);
  auto modNameStr = std::dynamic_pointer_cast<StrictString>(modName);
  if (modNameStr == nullptr) {
    return nullptr;
  }
  return loader_->loadModuleValue(modNameStr->getValue() + "." + name);
}

void Analyzer::visitImport(const stmt_ty stmt) {
  /*
   * If given a name like `foo.bar`, here we try to load the real
   * `foo.bar` module first, but counter-intuitively we won't use it.
   * If we don't have an asname (i.e. just `import foo.bar`), all we
   * actually care about returning is the base module (`foo`). If we
   * do have an asname, we will re-import starting from `foo` and
   * allow attributes in parent modules to shadow submodules (e.g.
   * import `foo` and see if it has a `bar` attribute before falling
   * back to importing `foo.bar`). But we import the real submodule
   * first here anyway and check if it exists, because if it doesn't
   * that should be an import error regardless (i.e. you're not
   * allowed to `import foo.bar as x` if `foo` has a `bar` attribute
   * but there is no real module `foo.bar`). In normal Python doing
   * this import of the real submodule first is also necessary because
   * it populates the attribute on the parent module (importing
   * "foo.bar" has the side effect of writing a `bar` attribute on
   * `foo`). In strict packages we don't allow that side effect, so
   * the only purpose of loading the full submodule here is to match
   * the runtime in failing the import if it doesn't exist.
   */
  auto importNames = stmt->v.Import.names;
  int n = asdl_seq_LEN(importNames);
  for (int i = 0; i < n; ++i) {
    alias_ty alias = reinterpret_cast<alias_ty>(asdl_seq_GET(importNames, i));
    std::string modName(PyUnicode_AsUTF8(alias->name));
    std::shared_ptr<StrictModuleObject> leafMod =
        loader_->loadModuleValue(modName);
    std::shared_ptr<BaseStrictObject> mod;
    std::string asName = importedNameHelper(alias);
    std::size_t split = modName.find('.');
    std::string baseName = modName.substr(0, split);
    if (leafMod != nullptr) {
      mod = loader_->loadModuleValue(baseName);
      if (alias->asname != nullptr) {
        while (split != std::string::npos) {
          if (mod == nullptr) {
            break;
          }
          std::size_t nextSplit = modName.find('.', split + 1);
          mod = iImportFrom(
              std::move(mod),
              modName.substr(split + 1, nextSplit),
              context_,
              loader_);
          split = nextSplit;
        }
      }
    }
    if (mod == nullptr && alias->asname == nullptr) {
      mod = makeUnknown(context_, "<imported module {}>", baseName);
    } else if (mod == nullptr) {
      mod =
          makeUnknown(context_, "<imported module {} as {}>", modName, asName);
    }
    stack_.set(std::move(asName), std::move(mod));
  }
}

void Analyzer::visitImportFrom(const stmt_ty stmt) {
  auto importFrom = stmt->v.ImportFrom;
  bool hasFromName = importFrom.module != nullptr;
  std::string fromName = hasFromName ? PyUnicode_AsUTF8(importFrom.module) : "";
  if (fromName == "__future__") {
    return;
  }
  std::string importedName;
  if (importFrom.level > 0) {
    // relative import
    int level = importFrom.level;
    std::size_t endPos = std::string::npos;
    for (int i = 0; i < level; ++i) {
      endPos = modName_.rfind('.', endPos - 1);
    }
    std::string relativeName = modName_.substr(0, endPos);
    if (hasFromName) {
      importedName.reserve(endPos + fromName.size() + 1);
      importedName.append(relativeName);
      importedName.append(".");
      importedName.append(fromName);
    } else {
      importedName = relativeName;
    }
  } else if (hasFromName) {
    importedName = fromName;
  }
  AnalysisResult mod = nullptr;

  bool useLazy = stack_.isGlobalScope();
  if (!useLazy) {
    mod = loader_->loadModuleValue(importedName);
  }

  int namesSize = asdl_seq_LEN(importFrom.names);
  for (int i = 0; i < namesSize; ++i) {
    alias_ty alias =
        reinterpret_cast<alias_ty>(asdl_seq_GET(importFrom.names, i));
    std::string aliasName = PyUnicode_AsUTF8(alias->name);
    if (aliasName == "*") {
      // star import is prohibited
      context_.error<StarImportDisallowedException>(importedName);
      continue;
    }
    std::string displayName = std::string(importFrom.level, '.') + fromName;
    std::string nameToStore = importedNameHelper(alias);

    AnalysisResult modValue;
    if (mod != nullptr) {
      modValue = iImportFrom(mod, aliasName, context_, loader_);
    } else if (useLazy) {
      std::string unknownName;
      if (alias->asname == nullptr) {
        unknownName =
            fmt::format("<{} imported from {}>", aliasName, displayName);
      } else {
        unknownName = fmt::format(
            "<{} imported from {} as {}>", aliasName, displayName, nameToStore);
      }
      modValue = std::make_shared<StrictLazyObject>(
          LazyObjectType(),
          context_.caller,
          loader_,
          importedName,
          std::move(unknownName),
          context_,
          aliasName);
      loader_->recordLazyModule(importedName);
    }

    if (modValue != nullptr) {
      stack_.set(std::move(nameToStore), std::move(modValue));
    } else if (alias->asname == nullptr) {
      AnalysisResult unknown = makeUnknown(
          context_, "<{} imported from {}>", aliasName, displayName);
      stack_.set(std::move(nameToStore), std::move(unknown));
    } else {
      AnalysisResult unknown = makeUnknown(
          context_,
          "<{} imported from {} as {}>",
          aliasName,
          displayName,
          nameToStore);
      stack_.set(std::move(nameToStore), std::move(unknown));
    }
  }
}

void Analyzer::visitAssign(const stmt_ty stmt) {
  auto assignStmt = stmt->v.Assign;
  AnalysisResult value = visitExpr(assignStmt.value);
  if (value) {
    for (int i = 0; i < asdl_seq_LEN(assignStmt.targets); ++i) {
      expr_ty target =
          reinterpret_cast<expr_ty>(asdl_seq_GET(assignStmt.targets, i));
      assignToTarget(target, value);
    }
  }
}

void Analyzer::visitExprStmt(const stmt_ty stmt) {
  expr_ty expr = stmt->v.Expr.value;
  visitExpr(expr);
}

void Analyzer::visitFunctionDef(const stmt_ty stmt) {
  auto f = stmt->v.FunctionDef;
  std::string funcName = PyUnicode_AsUTF8(f.name);
  AnalysisResult func = visitFunctionDefHelper(
      funcName,
      f.args,
      f.body,
      f.decorator_list,
      f.returns,
      f.type_comment,
      stmt->lineno,
      stmt->col_offset,
      stmt,
      false);
  // put function in scope
  stack_.set(std::move(funcName), std::move(func));
}
void Analyzer::visitAsyncFunctionDef(const stmt_ty stmt) {
  auto f = stmt->v.AsyncFunctionDef;
  std::string funcName = PyUnicode_AsUTF8(f.name);
  AnalysisResult func = visitFunctionDefHelper(
      funcName,
      f.args,
      f.body,
      f.decorator_list,
      f.returns,
      f.type_comment,
      stmt->lineno,
      stmt->col_offset,
      stmt,
      true);
  stack_.set(std::move(funcName), std::move(func));
}

AnalysisResult Analyzer::visitAnnotationHelper(expr_ty annotation) {
  if (futureAnnotations_) {
    Ref<> annotationStr = Ref<>::steal(_PyAST_ExprAsUnicode(annotation));
    return std::make_shared<StrictString>(
        StrType(), context_.caller, std::move(annotationStr));
  } else {
    return visitExpr(annotation);
  }
}

void Analyzer::addToDunderAnnotationsHelper(
    expr_ty target,
    AnalysisResult value) {
  if (!stack_.localContains(kDunderAnnotations)) {
    auto annotationsDict =
        std::make_shared<StrictDict>(DictObjectType(), context_.caller);
    stack_.localSet(kDunderAnnotations, std::move(annotationsDict));
  }

  auto key = std::make_shared<StrictString>(
      StrType(), context_.caller, Ref<>::create(target->v.Name.id));
  auto dunderAnnotationsDict = getFromScope(kDunderAnnotations);
  assert(dunderAnnotationsDict != std::nullopt);
  iSetElement(
      std::move(dunderAnnotationsDict.value()),
      std::move(key),
      std::move(value),
      context_);
}

void Analyzer::visitArgHelper(arg_ty arg, DictDataT& annotations) {
  if (arg->annotation == nullptr) {
    return;
  }
  AnalysisResult key = std::make_shared<StrictString>(
      StrType(), context_.caller, Ref<>::create(arg->arg));
  annotations[std::move(key)] = visitAnnotationHelper(arg->annotation);
}

void Analyzer::visitArgHelper(
    std::vector<std::string>& args,
    arg_ty arg,
    DictDataT& annotations) {
  args.emplace_back(PyUnicode_AsUTF8(arg->arg));
  if (arg->annotation != nullptr) {
    visitArgHelper(arg, annotations);
  }
}

AnalysisResult Analyzer::visitFunctionDefHelper(
    std::string funcName,
    arguments_ty args,
    asdl_stmt_seq* body,
    asdl_expr_seq* decoratorList,
    expr_ty returns,
    string, // type_comment
    int lineno,
    int col_offset,
    void* node,
    bool isAsync) {
  // symbol table for function body
  SymtableEntry symbols = stack_.getSymtable().entryFromAst(node);
  // function name and qualname
  std::string qualName = stack_.getQualifiedScopeName();
  if (qualName.empty()) {
    qualName = funcName;
  } else {
    qualName.append(".");
    qualName.append(funcName);
  }
  // function body
  int bodySize = asdl_seq_LEN(body);
  std::vector<stmt_ty> bodyVec;
  bodyVec.reserve(bodySize);
  for (int i = 0; i < bodySize; ++i) {
    bodyVec.push_back(reinterpret_cast<stmt_ty>(asdl_seq_GET(body, i)));
  }
  // arguments
  std::vector<std::string> posonlyArgs;
  std::vector<std::string> posArgs;
  std::vector<std::string> kwonlyArgs;
  std::optional<std::string> varArg;
  std::optional<std::string> kwVarArg;
  DictDataT annotations;

  int posonlySize = asdl_seq_LEN(args->posonlyargs);
  posonlyArgs.reserve(posonlySize);
  for (int i = 0; i < posonlySize; ++i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->posonlyargs, i));
    visitArgHelper(posonlyArgs, a, annotations);
  }

  int posSize = asdl_seq_LEN(args->args);
  posArgs.reserve(posSize);
  for (int i = 0; i < posSize; ++i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->args, i));
    visitArgHelper(posArgs, a, annotations);
  }

  if (args->vararg != nullptr) {
    varArg.emplace(PyUnicode_AsUTF8(args->vararg->arg));
    visitArgHelper(args->vararg, annotations);
  }

  int kwSize = asdl_seq_LEN(args->kwonlyargs);
  kwonlyArgs.reserve(kwSize);
  for (int i = 0; i < kwSize; ++i) {
    arg_ty a = reinterpret_cast<arg_ty>(asdl_seq_GET(args->kwonlyargs, i));
    visitArgHelper(kwonlyArgs, a, annotations);
  }

  if (args->kwarg != nullptr) {
    kwVarArg.emplace(PyUnicode_AsUTF8(args->kwarg->arg));
    visitArgHelper(args->kwarg, annotations);
  }
  // argument defaults
  std::vector<AnalysisResult> posDefaults;
  std::vector<AnalysisResult> kwDefaults;

  int kwDefaultSize = asdl_seq_LEN(args->kw_defaults);
  kwDefaults.reserve(kwDefaultSize);
  for (int i = 0; i < kwDefaultSize; ++i) {
    expr_ty d = reinterpret_cast<expr_ty>(asdl_seq_GET(args->kw_defaults, i));
    if (d == nullptr) {
      kwDefaults.push_back(nullptr);
    } else {
      kwDefaults.push_back(visitExpr(d));
    }
  }

  int posDefaultSize = asdl_seq_LEN(args->defaults);
  posDefaults.reserve(posDefaultSize);
  for (int i = 0; i < posDefaultSize; ++i) {
    expr_ty d = reinterpret_cast<expr_ty>(asdl_seq_GET(args->defaults, i));
    posDefaults.push_back(visitExpr(d));
  }

  // annotations object
  if (returns != nullptr) {
    annotations[context_.makeStr("return")] = visitAnnotationHelper(returns);
  }

  std::shared_ptr<StrictDict> annotationsObj = std::make_shared<StrictDict>(
      DictObjectType(), context_.caller, std::move(annotations));

  AnalysisResult func(new StrictFunction(
      FunctionType(),
      context_.caller,
      funcName,
      std::move(qualName),
      lineno,
      col_offset,
      std::move(bodyVec),
      stack_.getFunctionScope(),
      std::move(symbols),
      std::move(posonlyArgs),
      std::move(posArgs),
      std::move(kwonlyArgs),
      std::move(varArg),
      std::move(kwVarArg),
      std::move(posDefaults),
      std::move(kwDefaults),
      loader_,
      context_.filename,
      modName_,
      std::nullopt,
      std::move(annotationsObj),
      futureAnnotations_,
      isAsync));

  // rewriter attrs
  (*astToResults_)[node] = func;

  // decorators
  int decoratorSize = asdl_seq_LEN(decoratorList);
  // decorators should be applied in reverse order
  for (int i = decoratorSize - 1; i >= 0; --i) {
    expr_ty dec = reinterpret_cast<expr_ty>(asdl_seq_GET(decoratorList, i));
    AnalysisResult decObj = visitExpr(dec);
    (*astToResults_)[dec] = decObj;
    // call decorators, fix lineno
    {
      auto contextManager = updateContextHelper(dec->lineno, dec->col_offset);
      AnalysisResult obj = iCall(decObj, {func}, kEmptyArgNames, context_);
      func.swap(obj);
    }
  }

  return func;
}

void Analyzer::visitReturn(const stmt_ty stmt) {
  expr_ty returnV = stmt->v.Return.value;
  if (returnV == nullptr) {
    throw(FunctionReturnException(NoneObject()));
  }
  AnalysisResult returnVal = visitExpr(stmt->v.Return.value);
  throw(FunctionReturnException(std::move(returnVal)));
}

static AnalysisResult strDictToObjHelper(
    std::shared_ptr<DictType> dict,
    const CallerContext& caller) {
  DictDataT dictObj;
  dictObj.reserve(dict->size());
  for (auto& item : *dict) {
    dictObj[caller.makeStr(item.first)] = item.second.first;
  }
  return std::make_shared<StrictDict>(
      DictObjectType(), caller.caller, std::move(dictObj));
}

static bool isNoSlotBuiltinType(AnalysisResult value) {
  static std::vector<AnalysisResult> noSlotTypes{
      IntType(), TupleType(), SetType(), TypeType()};
  auto typ = std::dynamic_pointer_cast<StrictType>(value);
  if (typ) {
    for (auto& base : typ->mro()) {
      if (std::find(noSlotTypes.begin(), noSlotTypes.end(), base) !=
          noSlotTypes.end()) {
        return true;
      }
    }
  }
  return false;
}

void Analyzer::visitClassDef(const stmt_ty stmt) {
  auto classDef = stmt->v.ClassDef;
  // Step 1, identify metaclass
  std::shared_ptr<BaseStrictObject> metaclass;
  std::vector<AnalysisResult> bases = visitListLikeHelper(classDef.bases);
  // register metaclass if found in keyword args
  // find if any base class has metaclass
  int kwSize = asdl_seq_LEN(classDef.keywords);
  std::vector<std::string> kwargKeys;
  std::vector<AnalysisResult> kwargValues;
  kwargKeys.reserve(kwSize);
  kwargValues.reserve(kwSize);
  for (int i = 0; i < kwSize; ++i) {
    keyword_ty kw =
        reinterpret_cast<keyword_ty>(asdl_seq_GET(classDef.keywords, i));
    AnalysisResult kwVal = visitExpr(kw->value);
    if (PyUnicode_CompareWithASCIIString(kw->arg, "metaclass") == 0) {
      metaclass = std::move(kwVal);
    } else {
      kwargKeys.push_back(PyUnicode_AsUTF8(kw->arg));
      kwargValues.push_back(std::move(kwVal));
    }
  }

  bool replacedBases = false;
  AnalysisResult origBases;

  if (metaclass == nullptr && !bases.empty()) {
    // check if __mro_entries__ is defined for any bases
    std::vector<AnalysisResult> newBases;
    newBases.reserve(bases.size());
    auto baseTuple =
        std::make_shared<StrictTuple>(TupleType(), context_.caller, bases);

    for (auto& base : bases) {
      auto baseType = base->getType();
      if (baseType == UnknownType() ||
          std::dynamic_pointer_cast<StrictType>(base) != nullptr) {
        newBases.push_back(base);
        continue;
      }
      auto mroEntriesFunc =
          iLoadAttr(base, "__mro_entries__", nullptr, context_);
      if (mroEntriesFunc != nullptr) {
        auto newBaseEntries =
            iCall(mroEntriesFunc, {baseTuple}, kEmptyArgNames, context_);
        if (newBaseEntries != nullptr) {
          auto newBaseVec =
              iGetElementsVec(std::move(newBaseEntries), context_);
          newBases.insert(
              newBases.end(),
              std::move_iterator(newBaseVec.begin()),
              std::move_iterator(newBaseVec.end()));
          replacedBases = true;
        }
      }
    }

    if (replacedBases) {
      origBases = baseTuple;
    }
    std::swap(bases, newBases);

    // look for most common metaclass for all bases, skipping over
    // unknowns. Identify metaclass conflict
    if (metaclass == nullptr || !metaclass->isUnknown()) {
      auto metaclassType = std::dynamic_pointer_cast<StrictType>(metaclass);
      for (auto& base : bases) {
        auto baseTyp = std::dynamic_pointer_cast<StrictType>(base);
        if (baseTyp == nullptr) {
          continue;
        }
        auto baseTypMeta = baseTyp->getType();
        if (metaclassType == nullptr) {
          metaclassType = std::move(baseTypMeta);
        } else if (metaclassType->isSubType(baseTypMeta)) {
          continue;
        } else if (baseTypMeta->isSubType(metaclassType)) {
          metaclassType = std::move(baseTypMeta);
          continue;
        } else {
          context_.raiseTypeError("metaclass conflict");
        }
      }
      metaclass = metaclassType;
    }
  }
  if (metaclass == nullptr) {
    metaclass = TypeType();
  }

  // Step 2, run __prepare__ if exists, creating namespace ns
  std::shared_ptr<DictType> ns = std::make_shared<DictType>();
  AnalysisResult nsObj;
  std::string className = PyUnicode_AsUTF8(classDef.name);
  auto classNameObj = context_.makeStr(className);
  auto baseTupleObj =
      std::make_shared<StrictTuple>(TupleType(), context_.caller, bases);
  if (metaclass->getType() == UnknownType()) {
    context_.error<UnknownValueCallException>(metaclass->getDisplayName());
  } else {
    auto prepareFunc = iLoadAttr(metaclass, "__prepare__", nullptr, context_);
    if (prepareFunc != nullptr) {
      nsObj = iCall(
          prepareFunc, {classNameObj, baseTupleObj}, kEmptyArgNames, context_);
    }
  }

  // Step 3, create a hidden scope containing __class__
  const ScopeT& currentScope = stack_.getCurrentScope();
  std::shared_ptr<DictType> hiddenDunderClassScopeDict =
      std::make_shared<DictType>();
  std::unique_ptr<ScopeT> hiddenDunderClassScope = std::make_unique<ScopeT>(
      currentScope.getSTEntry(),
      hiddenDunderClassScopeDict,
      currentScope.getScopeData(),
      true);

  // Step 4, visit statements in class body with __class__ scope
  // Then ns scope
  {
    auto hiddenDunderClassManager =
        stack_.enterScope(std::move(hiddenDunderClassScope));
    auto classContextManager = stack_.enterScopeByAst(stmt, ns);
    classContextManager.getScope()->setScopeData(
        AnalysisScopeData(context_, nullptr, nsObj));
    visitStmtSeq(classDef.body);
  }

  // Step 5, extract the resulting ns scope, add __orig_bases__
  // if mro entries is used in step 1
  if (origBases != nullptr) {
    if (nsObj == nullptr) {
      (*ns)["__orig_bases__"] = std::move(origBases);
    } else {
      iSetElement(
          nsObj,
          context_.makeStr("__orig_bases__"),
          std::move(origBases),
          context_);
    }
  }
  AnalysisResult classDict = nsObj;
  if (classDict == nullptr) {
    classDict = strDictToObjHelper(std::move(ns), context_);
  }
  // Additional step, add __module__ to the class dict
  if (!iContainsElement(classDict, context_.makeStr("__module__"), context_)) {
    // getting __name__ from creator may not be accurate.
    // But we probably don't care about __module__ being accurate
    iSetElement(
        classDict,
        context_.makeStr("__module__"),
        context_.makeStr(modName_),
        context_);
  }

  // Step 6, call metaclass with class name, bases, ns, and kwargs
  std::vector<AnalysisResult> classCallArg{
      std::move(classNameObj), std::move(baseTupleObj), classDict};
  classCallArg.insert(
      classCallArg.end(),
      std::move_iterator(kwargValues.begin()),
      std::move_iterator(kwargValues.end()));
  auto classObj =
      iCall(metaclass, std::move(classCallArg), std::move(kwargKeys), context_);
  // Step 7, apply decorators
  int decoratorSize = asdl_seq_LEN(classDef.decorator_list);
  // decorators should be applied in reverse order
  for (int i = decoratorSize - 1; i >= 0; --i) {
    expr_ty dec =
        reinterpret_cast<expr_ty>(asdl_seq_GET(classDef.decorator_list, i));
    AnalysisResult decObj = visitExpr(dec);
    // call decorators, fix lineno
    {
      auto contextManager = updateContextHelper(dec->lineno, dec->col_offset);
      AnalysisResult tempClass =
          iCall(decObj, {classObj}, kEmptyArgNames, context_);
      std::swap(classObj, tempClass);
    }
  }
  // Additional step, add rewriter attrs of the class definition
  if (isNoSlotBuiltinType(classObj)) {
    classObj->ensureRewriterAttrs().setSlotsEnabled(false);
  }
  (*astToResults_)[stmt] = classObj;

  // Step 8, populate __class__ in hidden scope defined in step 3
  (*hiddenDunderClassScopeDict)[kDunderClass] = classObj;
  stack_.set(className, std::move(classObj));
}

void Analyzer::visitPass(const stmt_ty) {}

void Analyzer::visitDelete(const stmt_ty stmt) {
  auto delStmt = stmt->v.Delete;
  for (int i = 0; i < asdl_seq_LEN(delStmt.targets); ++i) {
    expr_ty target =
        reinterpret_cast<expr_ty>(asdl_seq_GET(delStmt.targets, i));
    assignToTarget(target, nullptr);
  }
}

void Analyzer::visitAugAssign(const stmt_ty stmt) {
  // TODO: this is not quite accurate, the correct dunder methods
  // to use are __i<op>__ methods. But we have been using normal
  // bin ops just fine. Add __i<op>__ methods if needed
  auto augAssign = stmt->v.AugAssign;
  AnalysisResult leftV = visitExpr(augAssign.target);
  AnalysisResult rightV = visitExpr(augAssign.value);
  AnalysisResult result =
      iDoBinOp(std::move(leftV), std::move(rightV), augAssign.op, context_);
  assignToTarget(augAssign.target, std::move(result));
}
void Analyzer::visitAnnAssign(const stmt_ty stmt) {
  auto annAssign = stmt->v.AnnAssign;
  expr_ty target = annAssign.target;

  if (annAssign.value != nullptr) {
    assignToTarget(target, visitExpr(annAssign.value));
  }
  bool classScope = stack_.isClassScope();
  bool evalAnnotations = classScope || stack_.isGlobalScope();
  if (target->kind == Name_kind && evalAnnotations) {
    AnalysisResult type = visitAnnotationHelper(annAssign.annotation);
    if (classScope) {
      addToDunderAnnotationsHelper(target, std::move(type));
    }
  }
}

void Analyzer::visitFor(const stmt_ty stmt) {
  auto forExpr = stmt->v.For;
  AnalysisResult iterObj = visitExpr(forExpr.iter);
  bool hasBreak = false;

  for (auto& element : iGetElementsVec(iterObj, context_)) {
    assignToTarget(forExpr.target, element);
    try {
      visitStmtSeq(forExpr.body);
    } catch (const LoopContinueException&) {
    } catch (const LoopBreakException&) {
      hasBreak = true;
      break;
    }
  }

  if (!hasBreak) {
    visitStmtSeq(forExpr.orelse);
  }
}

void Analyzer::visitWhile(const stmt_ty stmt) {
  int iterCount = 0;
  bool hasBreak = false;
  auto whileStmt = stmt->v.While;

  while (true) {
    // check too many iterations
    if (++iterCount > kIterationLimit) {
      context_.error<StrictModuleTooManyIterationsException>();
      break;
    }
    // check guard
    AnalysisResult test = iGetTruthValue(visitExpr(whileStmt.test), context_);
    if (test != StrictTrue()) {
      break;
    }
    // execute body
    try {
      visitStmtSeq(whileStmt.body);
    } catch (const LoopContinueException&) {
    } catch (const LoopBreakException&) {
      hasBreak = true;
      break;
    }
  }
  // orelse if no break
  if (!hasBreak) {
    visitStmtSeq(whileStmt.orelse);
  }
}

void Analyzer::visitIf(const stmt_ty stmt) {
  auto ifStmt = stmt->v.If;
  AnalysisResult testBool = iGetTruthValue(visitExpr(ifStmt.test), context_);
  if (testBool == StrictTrue()) {
    visitStmtSeq(ifStmt.body);
  } else if (testBool == StrictFalse()) {
    visitStmtSeq(ifStmt.orelse);
  }
}

void Analyzer::visitWith(const stmt_ty stmt) {
  auto withStmt = stmt->v.With;
  int withItemSize = asdl_seq_LEN(withStmt.items);
  std::vector<AnalysisResult> contexts;
  contexts.reserve(withItemSize);
  // enter and record context
  for (int i = 0; i < withItemSize; ++i) {
    withitem_ty item =
        reinterpret_cast<withitem_ty>(asdl_seq_GET(withStmt.items, i));
    AnalysisResult context = visitExpr(item->context_expr);
    auto enterFunc = iLoadAttrOnType(
        context,
        "__enter__",
        makeUnknown(context_, "{}.__enter__", context),
        context_);
    auto enterItem =
        iCall(std::move(enterFunc), kEmptyArgs, kEmptyArgNames, context_);
    if (item->optional_vars != nullptr) {
      assignToTarget(item->optional_vars, std::move(enterItem));
    }
    contexts.push_back(std::move(context));
  }
  // body
  AnalysisResult returnVal;
  std::optional<StrictModuleUserException<BaseStrictObject>> userExc;
  AnalysisResult excType = NoneObject();
  AnalysisResult excVal = NoneObject();
  AnalysisResult excTb = NoneObject();

  try {
    visitStmtSeq(withStmt.body);
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    userExc = e;
    excType = e.getWrapped()->getType();
    excVal = e.getWrapped();
    excTb = makeUnknown(context_, "traceback of {}", excVal);
  } catch (FunctionReturnException& r) {
    returnVal = r.getVal();
  }

  // exit contexts, in reverse order
  bool suppressExc = false;
  for (auto i = contexts.rbegin(); i != contexts.rend(); ++i) {
    AnalysisResult context = *i;
    auto exitFunc = iLoadAttrOnType(
        context,
        "__exit__",
        makeUnknown(context_, "{}.__exit__", context),
        context_);
    auto exitResult = iCall(
        std::move(exitFunc),
        {excType, excVal, excTb},
        kEmptyArgNames,
        context_);
    auto exitResultBool = iGetTruthValue(std::move(exitResult), context_);
    suppressExc |= exitResultBool == StrictTrue();
  }

  if (!suppressExc && userExc.has_value()) {
    throw userExc.value();
  }
  if (returnVal != nullptr) {
    throw FunctionReturnException(std::move(returnVal));
  }
}

void Analyzer::visitRaise(const stmt_ty stmt) {
  auto raiseStmt = stmt->v.Raise;
  expr_ty excExpr = raiseStmt.exc;
  AnalysisResult exc;
  if (excExpr != nullptr) {
    exc = visitExpr(excExpr);
    if (std::dynamic_pointer_cast<StrictType>(exc)) {
      // call the exception type
      exc = iCall(std::move(exc), kEmptyArgs, kEmptyArgNames, context_);
    }
  } else {
    exc = currentExceptionContext_;
    if (exc == nullptr) {
      log("%s: Cannot raise exceptions, no exceptions are active!",
          context_.filename.c_str());
      context_.raiseExceptionStr(RuntimeErrorType(), "no active exceptions");
    }
  }
  expr_ty excCause = raiseStmt.cause;
  if (excCause != nullptr) {
    visitExpr(excCause);
  }
  context_.raiseExceptionFromObj(std::move(exc));
}

bool Analyzer::visitExceptionHandlerHelper(
    asdl_excepthandler_seq* handlers,
    AnalysisResult exc) {
  int handlersSize = asdl_seq_LEN(handlers);
  for (int i = 0; i < handlersSize; ++i) {
    excepthandler_ty handler =
        reinterpret_cast<excepthandler_ty>(asdl_seq_GET(handlers, i));
    {
      // set the correct lineno
      auto _ = AnalysisContextManager(
          context_, handler->lineno, handler->col_offset);

      auto handlerV = handler->v.ExceptHandler;
      bool matched = false;
      if (handlerV.type != nullptr) {
        matched =
            isinstanceImpl(nullptr, context_, exc, visitExpr(handlerV.type)) ==
            StrictTrue();
      } else {
        // match against everything
        matched = true;
      }
      if (matched) {
        currentExceptionContext_ = exc;
        std::string excName;
        if (handlerV.name != nullptr) {
          excName = PyUnicode_AsUTF8(handlerV.name);
          stack_.set(excName, std::move(exc));
          visitStmtSeq(handlerV.body);
          stack_.erase(excName);
        } else {
          visitStmtSeq(handlerV.body);
        }
        currentExceptionContext_ = nullptr;
        return true;
      }
    }
  }
  return false;
}

void Analyzer::visitTry(const stmt_ty stmt) {
  auto tryStmt = stmt->v.Try;
  bool caughtException = false;

  try {
    try {
      visitStmtSeq(tryStmt.body);
    } catch (StrictModuleUserException<BaseStrictObject>& e) {
      caughtException = true;
      bool handledException =
          visitExceptionHandlerHelper(tryStmt.handlers, e.getWrapped());
      if (!handledException) {
        throw;
      }
    }
    if (!caughtException) {
      visitStmtSeq(tryStmt.orelse);
    }
  } catch (StrictModuleUserException<BaseStrictObject>& e) {
    visitStmtSeq(tryStmt.finalbody);
    throw;
  }

  visitStmtSeq(tryStmt.finalbody);
}

void Analyzer::visitAssert(const stmt_ty) {}

void Analyzer::visitBreak(const stmt_ty) {
  throw LoopBreakException();
}

void Analyzer::visitContinue(const stmt_ty) {
  throw LoopContinueException();
}

void Analyzer::visitGlobal(const stmt_ty) {}

void Analyzer::visitMatch(const stmt_ty) {
  context_.error<UnsupportedException>("match statement", "");
}

// Expressions
AnalysisResult Analyzer::visitConstant(const expr_ty expr) {
  auto constant = expr->v.Constant;
  if (PyLong_CheckExact(constant.value)) {
    auto value =
        std::make_shared<StrictInt>(IntType(), context_.caller, constant.value);
    return value;
  }
  if (PyUnicode_CheckExact(constant.value)) {
    auto value = std::make_shared<StrictString>(
        StrType(), context_.caller, Ref<>(Ref<>::create(constant.value)));
    return value;
  }
  if (PyFloat_CheckExact(constant.value)) {
    auto value = std::make_shared<StrictFloat>(
        FloatType(), context_.caller, constant.value);
    return value;
  }
  if (PyBytes_CheckExact(constant.value)) {
    auto value = std::make_shared<StrictBytes>(
        BytesType(), context_.caller, constant.value);
    return value;
  }
  if (constant.value == Py_None) {
    return NoneObject();
  }
  if (constant.value == Py_True) {
    return StrictTrue();
  }
  if (constant.value == Py_False) {
    return StrictFalse();
  }
  if (constant.value == Py_Ellipsis) {
    return EllipsisObject();
  }
  return defaultVisitExpr();
}

AnalysisResult Analyzer::visitName(const expr_ty expr) {
  auto name = expr->v.Name;
  const char* nameStr = PyUnicode_AsUTF8(name.id);
  auto value = getFromScope(nameStr);
  if (!value) {
    // TODO? decide whether to raise NameError or UnboundLocalError base on
    // declaration
    log("%s: Encountered a NameError (%s)", context_.filename.c_str(), nameStr);
    context_.raiseExceptionStr(
        NameErrorType(), "name {} is not defined", nameStr);
  }
  return value.value();
}

AnalysisResult Analyzer::visitAttribute(const expr_ty expr) {
  auto attribute = expr->v.Attribute;
  AnalysisResult value = visitExpr(attribute.value);
  assert(value != nullptr);
  const char* attrName = PyUnicode_AsUTF8(attribute.attr);
  assert(attribute.ctx != Del);
  auto result = iLoadAttr(value, attrName, nullptr, context_);
  if (!result) {
    auto typeName = value->getTypeRef().getName();
    log("%s: Encountered an AttributeError (%s has no %s)",
        context_.filename.c_str(),
        typeName.c_str(),
        attrName);
    context_.raiseExceptionStr(
        AttributeErrorType(),
        "{} object has no attribute {}",
        typeName,
        attrName);
  }
  return result;
}

AnalysisResult Analyzer::visitCall(const expr_ty expr) {
  auto call = expr->v.Call;
  AnalysisResult func = visitExpr(call.func);
  assert(func != nullptr);

  auto argsSeq = call.args;
  auto kwargsSeq = call.keywords;
  int argsLen = asdl_seq_LEN(argsSeq);
  int kwargsLen = asdl_seq_LEN(kwargsSeq);

  // Special handling of super() with no arguments
  if (argsLen == 0 && kwargsLen == 0 && func == SuperType()) {
    return callMagicalSuperHelper(std::move(func));
  }

  if (argsLen == 1 && kwargsLen == 0 && func == StrictTryImport()) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(argsSeq, 0));
    auto argV = visitExpr(argExpr);
    auto argStr = std::dynamic_pointer_cast<StrictString>(argV);
    if (argStr) {
      auto mod = loader_->tryGetModuleValue(argStr->getValue());
      return mod ? mod : NoneObject();
    }
  }

  std::vector<AnalysisResult> args;
  std::vector<std::string> argNames;
  args.reserve(argsLen + kwargsLen);
  argNames.reserve(kwargsLen);

  for (int i = 0; i < argsLen; ++i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(argsSeq, i));
    if (argExpr->kind == Starred_kind) {
      auto starredElts = visitExpr(argExpr);
      auto starredEltsVec = iGetElementsVec(std::move(starredElts), context_);
      args.insert(
          args.end(),
          std::move_iterator(starredEltsVec.begin()),
          std::move_iterator(starredEltsVec.end()));
    } else {
      args.push_back(visitExpr(argExpr));
    }
  }
  for (int i = 0; i < kwargsLen; ++i) {
    keyword_ty kw = reinterpret_cast<keyword_ty>(asdl_seq_GET(kwargsSeq, i));
    if (kw->arg != nullptr) {
      args.push_back(visitExpr(kw->value));
      argNames.emplace_back(PyUnicode_AsUTF8(kw->arg));
    } else {
      // double star arg
      auto doubleStarArg = visitExpr(kw->value);
      auto doubleStarDict =
          std::dynamic_pointer_cast<StrictDict>(doubleStarArg);
      if (doubleStarDict) {
        doubleStarDict->getData().const_iter(
            [&args, &argNames](AnalysisResult k, AnalysisResult v) {
              auto keyStr =
                  std::dynamic_pointer_cast<StrictString>(std::move(k));
              if (keyStr) {
                args.push_back(std::move(v));
                argNames.push_back(keyStr->getValue());
              }
              return true;
            });
      }
    }
  }
  return iCall(func, std::move(args), std::move(argNames), context_);
}

AnalysisResult Analyzer::callMagicalSuperHelper(AnalysisResult func) {
  // rule out cases where user explicitly define __class__ on module level
  auto dunderClass = getFromScope(kDunderClass);
  if (dunderClass == std::nullopt || stack_.isGlobal(kDunderClass)) {
    context_.raiseExceptionStr(
        RuntimeErrorType(), "super(): __class__ not found");
  }

  const AnalysisResult& firstArg =
      stack_.getCurrentScope().getScopeData().getCallFirstArg();
  if (firstArg == nullptr) {
    context_.raiseExceptionStr(RuntimeErrorType(), "super(): no arguments");
  }
  return iCall(
      std::move(func),
      {dunderClass.value(), firstArg},
      kEmptyArgNames,
      context_);
}

std::vector<AnalysisResult> Analyzer::visitListLikeHelper(asdl_expr_seq* elts) {
  int eltsLen = asdl_seq_LEN(elts);
  std::vector<AnalysisResult> data;
  data.reserve(eltsLen);
  for (int i = 0; i < eltsLen; ++i) {
    expr_ty argExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
    if (argExpr->kind == Starred_kind) {
      AnalysisResult starredVal = visitStarred(argExpr);
      std::vector<AnalysisResult> starredVec =
          iGetElementsVec(starredVal, context_);
      data.insert(
          data.end(),
          std::move_iterator(starredVec.begin()),
          std::move_iterator(starredVec.end()));
    } else {
      data.push_back(visitExpr(argExpr));
    }
  }
  return data;
}

AnalysisResult Analyzer::visitSet(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.Set.elts);
  SetDataT data(std::move_iterator(v.begin()), std::move_iterator(v.end()));
  AnalysisResult obj =
      std::make_shared<StrictSet>(SetType(), context_.caller, std::move(data));
  return obj;
}

AnalysisResult Analyzer::visitList(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.List.elts);
  AnalysisResult obj =
      std::make_shared<StrictList>(ListType(), context_.caller, std::move(v));
  return obj;
}

AnalysisResult Analyzer::visitTuple(const expr_ty expr) {
  auto v = visitListLikeHelper(expr->v.Tuple.elts);
  AnalysisResult obj =
      std::make_shared<StrictTuple>(TupleType(), context_.caller, std::move(v));
  return obj;
}

DictDataT Analyzer::visitDictUnpackHelper(expr_ty valueExpr) {
  AnalysisResult unpacked = visitExpr(valueExpr);
  auto keys = iGetElementsVec(unpacked, context_);

  DictDataT map;
  map.reserve(keys.size());
  for (auto& k : keys) {
    auto value = iGetElement(unpacked, k, context_);
    map[k] = std::move(value);
  }
  return map;
}

AnalysisResult Analyzer::visitDict(const expr_ty expr) {
  auto dict = expr->v.Dict;
  int keysLen = asdl_seq_LEN(dict.keys);
  assert(keysLen == asdl_seq_LEN(dict.values));

  DictDataT map;
  map.reserve(keysLen);

  for (int i = 0; i < keysLen; ++i) {
    expr_ty keyExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(dict.keys, i));
    expr_ty valueExpr = reinterpret_cast<expr_ty>(asdl_seq_GET(dict.values, i));
    if (keyExpr == nullptr) {
      // handle unpacking
      DictDataT unpackedMap = visitDictUnpackHelper(valueExpr);
      for (auto& item : unpackedMap) {
        map[std::move(item.first)] = std::move(item.second.first);
      }
    } else {
      AnalysisResult kResult = visitExpr(keyExpr);
      AnalysisResult vResult = visitExpr(valueExpr);
      map[kResult] = vResult;
    }
  }
  return std::make_shared<StrictDict>(
      DictObjectType(), context_.caller, std::move(map));
}

AnalysisResult Analyzer::visitBinOp(const expr_ty expr) {
  AnalysisResult left = visitExpr(expr->v.BinOp.left);
  AnalysisResult right = visitExpr(expr->v.BinOp.right);
  return iDoBinOp(
      std::move(left), std::move(right), expr->v.BinOp.op, context_);
}

AnalysisResult Analyzer::visitUnaryOp(const expr_ty expr) {
  AnalysisResult value = visitExpr(expr->v.UnaryOp.operand);
  unaryop_ty op = expr->v.UnaryOp.op;
  if (op == Not) {
    auto result = iGetTruthValue(std::move(value), context_);
    if (result->getType() == UnknownType()) {
      return result;
    }
    return context_.makeBool(result == StrictFalse());
  }
  return iUnaryOp(std::move(value), op, context_);
}

AnalysisResult Analyzer::visitCompare(const expr_ty expr) {
  auto compare = expr->v.Compare;
  AnalysisResult leftObj = visitExpr(compare.left);
  int cmpSize = asdl_seq_LEN(compare.ops);
  AnalysisResult compareValue;
  for (int i = 0; i < cmpSize; ++i) {
    cmpop_ty op = static_cast<cmpop_ty>(asdl_seq_GET(compare.ops, i));
    expr_ty rightExpr =
        reinterpret_cast<expr_ty>(asdl_seq_GET(compare.comparators, i));
    AnalysisResult rightObj = visitExpr(rightExpr);
    compareValue = iBinCmpOp(leftObj, rightObj, op, context_);
    AnalysisResult compareBool = iGetTruthValue(compareValue, context_);
    if (compareBool == StrictFalse()) {
      // short circuit
      return compareValue;
    }
    if (compareBool->getType() == UnknownType()) {
      // unknown result
      return compareValue;
    }
    leftObj = std::move(rightObj);
  }
  assert(compareValue != nullptr);
  return compareValue;
}

AnalysisResult Analyzer::visitBoolOp(const expr_ty expr) {
  auto boolOpExpr = expr->v.BoolOp;
  AnalysisResult result;
  int valuesSize = asdl_seq_LEN(boolOpExpr.values);
  for (int i = 0; i < valuesSize; ++i) {
    expr_ty e = reinterpret_cast<expr_ty>(asdl_seq_GET(boolOpExpr.values, i));
    result = visitExpr(e);
    auto resultBool = iGetTruthValue(result, context_);
    if (boolOpExpr.op == And && resultBool == StrictFalse()) {
      break;
    } else if (boolOpExpr.op == Or && resultBool == StrictTrue()) {
      break;
    } else if (resultBool->getType() == UnknownType()) {
      break;
    }
  }
  return result;
}

AnalysisResult Analyzer::visitNamedExpr(const expr_ty expr) {
  auto namedExpr = expr->v.NamedExpr;
  AnalysisResult value = visitExpr(namedExpr.value);
  assignToTarget(namedExpr.target, value);
  return value;
}

AnalysisResult Analyzer::visitSubscript(const expr_ty expr) {
  auto subscrExpr = expr->v.Subscript;
  AnalysisResult base = visitExpr(subscrExpr.value);
  AnalysisResult idx = visitExpr(subscrExpr.slice);
  return iGetElement(std::move(base), std::move(idx), context_);
}

AnalysisResult Analyzer::visitStarred(const expr_ty expr) {
  return visitExpr(expr->v.Starred.value);
}

AnalysisResult Analyzer::visitSlice(const expr_ty expr) {
  auto sliceExp = expr->v.Slice;
  AnalysisResult start =
      sliceExp.lower ? visitExpr(sliceExp.lower) : NoneObject();
  AnalysisResult stop =
      sliceExp.upper ? visitExpr(sliceExp.upper) : NoneObject();
  AnalysisResult step = sliceExp.step ? visitExpr(sliceExp.step) : NoneObject();
  return std::make_shared<StrictSlice>(
      SliceType(),
      context_.caller,
      std::move(start),
      std::move(stop),
      std::move(step));
}

AnalysisResult Analyzer::visitLambda(const expr_ty expr) {
  auto lambdaExp = expr->v.Lambda;
  stmt_ty returnStmt = _PyAST_Return(
      lambdaExp.body,
      expr->lineno,
      expr->col_offset,
      expr->end_lineno,
      expr->end_col_offset,
      loader_->getArena());
  asdl_stmt_seq* body = _Py_asdl_stmt_seq_new(1, loader_->getArena());
  asdl_seq_SET(body, 0, returnStmt);
  return visitFunctionDefHelper(
      "<lambda>",
      lambdaExp.args,
      body,
      nullptr,
      nullptr,
      nullptr,
      expr->lineno,
      expr->col_offset,
      expr,
      false);
}

AnalysisResult Analyzer::visitIfExp(const expr_ty expr) {
  auto ifExp = expr->v.IfExp;
  AnalysisResult test = iGetTruthValue(visitExpr(ifExp.test), context_);
  if (test == StrictTrue()) {
    return visitExpr(ifExp.body);
  }
  if (test == StrictFalse()) {
    return visitExpr(ifExp.orelse);
  }
  AnalysisResult body = visitExpr(ifExp.body);
  AnalysisResult orelse = visitExpr(ifExp.orelse);
  return makeUnknown(context_, "({} if {} else {})", body, test, orelse);
}

AnalysisResult Analyzer::visitListComp(const expr_ty expr) {
  auto comp = expr->v.ListComp;
  std::vector<AnalysisResult> result;
  visitGeneratorHelper(
      expr,
      comp.generators,
      [&result](AnalysisResult v) { result.push_back(std::move(v)); },
      comp.elt);
  return std::make_shared<StrictList>(
      ListType(), context_.caller, std::move(result));
}

AnalysisResult Analyzer::visitSetComp(const expr_ty expr) {
  auto comp = expr->v.SetComp;
  SetDataT result;
  visitGeneratorHelper(
      expr,
      comp.generators,
      [&result](AnalysisResult v) { result.insert(std::move(v)); },
      comp.elt);
  return std::make_shared<StrictSet>(
      SetType(), context_.caller, std::move(result));
}

AnalysisResult Analyzer::visitDictComp(const expr_ty expr) {
  auto comp = expr->v.DictComp;
  DictDataT result;
  visitGeneratorHelper(
      expr,
      comp.generators,
      [&result](AnalysisResult k, AnalysisResult v) {
        result[std::move(k)] = std::move(v);
      },
      comp.key,
      comp.value);
  return std::make_shared<StrictDict>(
      DictObjectType(), context_.caller, std::move(result));
}
AnalysisResult Analyzer::visitGeneratorExp(const expr_ty expr) {
  auto comp = expr->v.GeneratorExp;
  std::vector<AnalysisResult> result;
  visitGeneratorHelper(
      expr,
      comp.generators,
      [&result](AnalysisResult v) { result.push_back(std::move(v)); },
      comp.elt);
  return std::make_shared<StrictGeneratorExp>(
      GeneratorExpType(), context_.caller, std::move(result));
}

template <typename CB, typename... Args>
void Analyzer::visitGeneratorHelper(
    expr_ty node,
    asdl_comprehension_seq* generators,
    CB callback,
    Args... targets) {
  int numComps = asdl_seq_LEN(generators);
  assert(numComps > 0);
  // the first comprehension does not have a separate inner scope
  comprehension_ty comp =
      reinterpret_cast<comprehension_ty>(asdl_seq_GET(generators, 0));
  AnalysisResult compValue = visitExpr(comp->iter);
  std::vector<comprehension_ty> comps;
  comps.reserve(numComps - 1);
  for (int i = 1; i < numComps; ++i) {
    comps.push_back(
        reinterpret_cast<comprehension_ty>(asdl_seq_GET(generators, i)));
  }
  {
    auto manager = stack_.enterScopeByAst(node);
    visitGeneratorHelperInner(
        std::move(compValue),
        comp->target,
        comp->ifs,
        comps,
        0,
        callback,
        targets...);
  }
}

template <typename CB, typename... Args>
void Analyzer::visitGeneratorHelperInner(
    AnalysisResult iter,
    expr_ty iterTarget,
    asdl_expr_seq* ifs,
    const std::vector<comprehension_ty>& comps,
    std::size_t idx,
    CB callback,
    Args... targets) {
  std::vector<AnalysisResult> elements =
      iGetElementsVec(std::move(iter), context_);

  if (idx == comps.size()) {
    // base case, produce value for targets
    for (AnalysisResult elem : elements) {
      assignToTarget(iterTarget, std::move(elem));
      if (!checkGeneratorIfHelper(ifs)) {
        continue;
      }
      callback(visitExpr(targets)...);
    }
    return;
  }

  comprehension_ty nextComp = comps[idx];
  for (AnalysisResult elem : elements) {
    assignToTarget(iterTarget, std::move(elem));
    if (!checkGeneratorIfHelper(ifs)) {
      continue;
    }
    visitGeneratorHelperInner(
        visitExpr(nextComp->iter),
        nextComp->target,
        nextComp->ifs,
        comps,
        idx + 1,
        callback,
        targets...);
  }
}

bool Analyzer::checkGeneratorIfHelper(asdl_expr_seq* ifs) {
  int size = asdl_seq_LEN(ifs);
  for (int i = 0; i < size; ++i) {
    expr_ty cond = reinterpret_cast<expr_ty>(asdl_seq_GET(ifs, i));
    AnalysisResult v = iGetTruthValue(visitExpr(cond), context_);
    if (v != StrictTrue()) {
      return false;
    }
  }
  return true;
}

AnalysisResult Analyzer::visitAwait(const expr_ty expr) {
  auto await = expr->v.Await;
  AnalysisResult awaitExpr = visitExpr(await.value);
  context_.error<UnsupportedException>("await", awaitExpr->getDisplayName());
  return makeUnknown(context_, "await {}", awaitExpr);
}

AnalysisResult Analyzer::visitYield(const expr_ty) {
  throw YieldReachedException();
}

AnalysisResult Analyzer::visitYieldFrom(const expr_ty) {
  throw YieldReachedException();
}

static AnalysisResult formatHelper(
    AnalysisResult str,
    AnalysisResult formatSpec,
    const CallerContext& caller) {
  auto formatFunc = iLoadAttrOnType(str, "__format__", nullptr, caller);
  if (formatFunc == nullptr) {
    return makeUnknown(caller, "{}.__format__{}", str, formatSpec);
  }
  AnalysisResult result = iCall(
      std::move(formatFunc), {std::move(formatSpec)}, kEmptyArgNames, caller);
  return result;
}

AnalysisResult Analyzer::visitFormattedValue(const expr_ty expr) {
  auto fv = expr->v.FormattedValue;
  AnalysisResult value = visitExpr(fv.value);
  switch (fv.conversion) {
    case -1:
      break;
    case 's': {
      // call str()
      value = iCall(StrType(), {value}, kEmptyArgNames, context_);
      break;
    }
    case 'r': {
      // call repr()
      value = reprImpl(nullptr, context_, value);
      break;
    }
    case 'a': {
      context_.error<UnsupportedException>(
          "'joined str to ascii'", value->getDisplayName());
    }
  }
  AnalysisResult formatSpec;
  if (fv.format_spec == nullptr) {
    formatSpec = context_.makeStr("");
  } else {
    formatSpec = visitExpr(fv.format_spec);
  }
  AnalysisResult result =
      formatHelper(std::move(value), std::move(formatSpec), context_);
  const BaseStrictObject& resultRef = *result;
  if (!result->isUnknown() && typeid(resultRef) != typeid(StrictString)) {
    context_.raiseTypeError(
        "result of format should be string, not {}",
        result->getTypeRef().getName());
  }
  return result;
}

AnalysisResult Analyzer::visitJoinedStr(const expr_ty expr) {
  auto joinedStr = expr->v.JoinedStr;
  int size = asdl_seq_LEN(joinedStr.values);
  std::string result;
  bool isUnknown = false;
  for (int i = 0; i < size; ++i) {
    expr_ty sectionExpr =
        reinterpret_cast<expr_ty>(asdl_seq_GET(joinedStr.values, i));
    AnalysisResult section = visitExpr(sectionExpr);
    auto sectionStr = std::dynamic_pointer_cast<StrictString>(section);
    if (sectionStr) {
      result += sectionStr->getValue();
    } else {
      isUnknown = true;
      result += section->getDisplayName();
    }
  }
  if (isUnknown) {
    return makeUnknown(context_, "{}", std::move(result));
  }
  return context_.makeStr(std::move(result));
}

void Analyzer::visitStmtSeq(const asdl_stmt_seq* seq) {
  for (int i = 0; i < asdl_seq_LEN(seq); i++) {
    stmt_ty elt = reinterpret_cast<stmt_ty>(asdl_seq_GET(seq, i));
    visitStmt(elt);
  }
}

void Analyzer::visitStmtSeq(std::vector<stmt_ty> seq) {
  for (const stmt_ty s : seq) {
    visitStmt(s);
  }
}

void Analyzer::defaultVisitMod() {
  raiseUnimplemented();
}

void Analyzer::defaultVisitStmt() {
  raiseUnimplemented();
}

AnalysisResult Analyzer::defaultVisitExpr() {
  raiseUnimplemented();
  return makeUnknown(context_, "<unimplemented expr>");
}

AnalysisContextManager Analyzer::updateContext(stmt_ty stmt) {
  return updateContextHelper(stmt->lineno, stmt->col_offset);
}
AnalysisContextManager Analyzer::updateContext(expr_ty expr) {
  return updateContextHelper(expr->lineno, expr->col_offset);
}
AnalysisContextManager Analyzer::updateContext(mod_ty) {
  return updateContextHelper(0, 0);
}

void Analyzer::assignToTarget(const expr_ty target, AnalysisResult value) {
  switch (target->kind) {
    case Name_kind:
      assignToName(target, std::move(value));
      break;
    case Tuple_kind:
      assignToListLike(target->v.Tuple.elts, std::move(value));
      break;
    case List_kind:
      assignToListLike(target->v.List.elts, std::move(value));
      break;
    case Attribute_kind:
      assignToAttribute(target, std::move(value));
      break;
    case Subscript_kind:
      assignToSubscript(target, std::move(value));
      break;
    case Starred_kind:
      assignToStarred(target, std::move(value));
      break;
    default:
      return;
  }
}

void Analyzer::assignToName(const expr_ty target, AnalysisResult value) {
  PyObject* id = target->v.Name.id;
  const char* name = PyUnicode_AsUTF8(id);
  if (value == nullptr) {
    stack_.erase(name);
  } else {
    stack_.set(name, std::move(value));
  }
}

void Analyzer::assignToListLike(asdl_expr_seq* elts, AnalysisResult value) {
  int eltsSize = asdl_seq_LEN(elts);

  // delete case
  if (value == nullptr) {
    for (int i = 0; i < eltsSize; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, nullptr);
    }
    return;
  }

  std::vector<AnalysisResult> rData =
      iGetElementsVec(std::move(value), context_);
  // check of there is star
  int starIdx = -1;
  for (int i = 0; i < eltsSize; ++i) {
    expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
    if (elt->kind == Starred_kind) {
      starIdx = i;
      break;
    }
  }

  // assignment with star on the lhs, starred exp cannot be used in delete
  if (starIdx >= 0 && eltsSize <= int(rData.size() + 1)) {
    // process part before star
    for (int i = 0; i < starIdx; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, rData[i]);
    }
    // process the star part
    int restSize = eltsSize - starIdx - 1;
    int tailBound = rData.size() - restSize;
    std::vector<AnalysisResult> starData(
        rData.begin() + starIdx, rData.begin() + tailBound);
    AnalysisResult starList = std::make_shared<StrictList>(
        ListType(), context_.caller, std::move(starData));
    expr_ty starElt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, starIdx));
    assignToTarget(starElt, std::move(starList));
    // process the part after star
    for (int i = 0; i < restSize; ++i) {
      expr_ty elt =
          reinterpret_cast<expr_ty>(asdl_seq_GET(elts, starIdx + i + 1));
      assignToTarget(elt, rData[tailBound + i]);
    }
  }
  // normal assign
  else if (eltsSize == int(rData.size())) {
    for (int i = 0; i < eltsSize; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, rData[i]);
    }
  } else {
    // failed to unpack
    context_.error<FailedToUnpackException>(std::to_string(eltsSize));
    for (int i = 0; i < eltsSize; ++i) {
      expr_ty elt = reinterpret_cast<expr_ty>(asdl_seq_GET(elts, i));
      assignToTarget(elt, makeUnknown(context_, "<failed unpack>"));
    }
  }
}

void Analyzer::assignToAttribute(const expr_ty attr, AnalysisResult value) {
  auto attrExpr = attr->v.Attribute;
  AnalysisResult base = visitExpr(attrExpr.value);
  std::string name = PyUnicode_AsUTF8(attrExpr.attr);
  if (value == nullptr) {
    iDelAttr(std::move(base), name, context_);
  } else {
    iStoreAttr(std::move(base), name, std::move(value), context_);
  }
}

void Analyzer::assignToSubscript(const expr_ty subscr, AnalysisResult value) {
  auto subExpr = subscr->v.Subscript;
  AnalysisResult base = visitExpr(subExpr.value);
  AnalysisResult slice = visitExpr(subExpr.slice);
  if (value == nullptr) {
    iDelElement(std::move(base), std::move(slice), context_);
  } else {
    iSetElement(std::move(base), std::move(slice), std::move(value), context_);
  }
}

void Analyzer::assignToStarred(const expr_ty starred, AnalysisResult value) {
  assignToTarget(starred->v.Starred.value, std::move(value));
}

void Analyzer::analyze() {
  try {
    visitMod(root_);
  } catch (StrictModuleUserException<BaseStrictObject>& exc) {
    processUnhandledUserException(exc);
  }
}

void Analyzer::analyzeFunction(
    std::vector<stmt_ty> body,
    SymtableEntry entry,
    std::unique_ptr<DictType> callArgs,
    AnalysisResult firstArg) {
  auto scope = Analyzer::scopeFactory(std::move(entry), std::move(callArgs));
  scope->setScopeData(AnalysisScopeData(std::move(firstArg)));
  // enter function body scope
  auto scopeManager = stack_.enterScope(std::move(scope));
  visitStmtSeq(std::move(body));
}

void Analyzer::processUnhandledUserException(
    StrictModuleUserException<BaseStrictObject>& exc) {
  std::shared_ptr<BaseStrictObject> wrappedObject =
      std::static_pointer_cast<BaseStrictObject>(exc.getWrapped());

  std::vector<std::string> args;
  if (!wrappedObject->isUnknown()) {
    auto wrappedInst = std::static_pointer_cast<StrictInstance>(wrappedObject);
    AnalysisResult argsV = wrappedInst->getAttr("args");
    auto argsSeq = std::dynamic_pointer_cast<StrictSequence>(argsV);
    if (argsSeq) {
      args.reserve(argsSeq->getData().size());
      for (auto& elem : argsSeq->getData()) {
        args.push_back(elem->getDisplayName());
      }
    }
  }
  context_.errorSink->error<StrictModuleUnhandledException>(
      exc.getLineno(),
      exc.getCol(),
      exc.getFilename(),
      exc.getScopeName(),
      wrappedObject->getDisplayName(),
      std::move(args),
      exc.getCause());
}

template <typename... Args>
void Analyzer::log(const char* fmt, Args... args) {
  if (loader_) {
    loader_->log(fmt, args...);
  }
}

std::unique_ptr<Analyzer::ScopeT> Analyzer::scopeFactory(
    SymtableEntry entry,
    std::shared_ptr<DictType> map) {
  return std::make_unique<ScopeT>(entry, std::move(map), AnalysisScopeData());
}

AnalysisResult Analyzer::analyzeExecOrEval(
    int callerLineno,
    int callerCol,
    std::shared_ptr<StrictDict> globals,
    std::shared_ptr<StrictDict> locals) {
  // fix the inner caller context's lineno and col
  context_.lineno = callerLineno;
  context_.col = callerCol;

  SymtableEntry entry = stack_.getSymtable().entryFromAst(root_);
  auto scope =
      Analyzer::scopeFactory(std::move(entry), std::make_unique<DictType>());
  scope->setScopeData(AnalysisScopeData(context_, nullptr, std::move(globals)));

  auto localScope =
      Analyzer::scopeFactory(std::move(entry), std::make_unique<DictType>());
  localScope->setScopeData(
      AnalysisScopeData(context_, nullptr, std::move(locals)));
  auto scopeManagerGlobals = stack_.enterScope(std::move(scope));
  auto scopeManagerLocals = stack_.enterScope(std::move(localScope));
  if (root_->kind == Module_kind) {
    visitMod(root_);
    return NoneObject();
  } else if (root_->kind == Expression_kind) {
    return visitExpr(root_->v.Expression.body);
  } else {
    raiseUnimplemented();
  }
  return nullptr;
}

std::optional<AnalysisResult> Analyzer::getFromScope(const std::string& name) {
  std::optional<AnalysisResult> result;
  ScopeT* resultScope;
  std::tie(result, resultScope) = stack_.at_and_scope(name);

  if (result.has_value() && (*result)->isLazy()) {
    assert(resultScope != nullptr);
    auto evaluated =
        std::static_pointer_cast<StrictLazyObject>(*result)->evaluate();
    resultScope->set(name, evaluated);
    return evaluated;
  } else {
    return result;
  }
}

// TryFinallyManager
TryFinallyManager::TryFinallyManager(
    Analyzer& analyzer,
    asdl_stmt_seq* finalbody)
    : analyzer_(analyzer), finalbody_(finalbody) {}

TryFinallyManager::~TryFinallyManager() {
  analyzer_.visitStmtSeq(finalbody_);
}

} // namespace strictmod
