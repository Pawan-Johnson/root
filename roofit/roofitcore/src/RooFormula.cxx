/// \cond ROOFIT_INTERNAL

/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 * @(#)root/roofitcore:$Id$
 * Authors:                                                                  *
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu       *
 *   DK, David Kirkby,    UC Irvine,         dkirkby@uci.edu                 *
 *                                                                           *
 * Copyright (c) 2000-2005, Regents of the University of California          *
 *                          and Stanford University. All rights reserved.    *
 *                                                                           *
 * Redistribution and use in source and binary forms,                        *
 * with or without modification, are permitted according to the terms        *
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)             *
 *****************************************************************************/

/**
\file RooFormula.cxx
\class RooFormula
\ingroup Roofitcore

Internally uses ROOT's TFormula to compute user-defined expressions of RooAbsArgs.

The string expression can be any valid TFormula expression referring to the
listed servers either by name or by their ordinal list position. These three are
forms equivalent:
```
  RooFormula("formula", "x*y",       RooArgList(x,y))  or
  RooFormula("formula", "@0*@1",     RooArgList(x,y))
  RooFormula("formula", "x[0]*x[1]", RooArgList(x,y))
```
Note that `x[i]` is an expression reserved for TFormula. If a variable with
the name `x` is given, the RooFormula interprets `x` as a variable name,
but `x[i]` as an index in the list of variables.

### Category expressions
State information of RooAbsCategories can be accessed using the '::' operator,
*i.e.*, `tagCat::Kaon` will resolve to the numerical value of
the `Kaon` state of the RooAbsCategory object named `tagCat`.

A formula to switch between lepton categories could look like this:
```
  RooFormula("formulaWithCat",
    "x * (leptonMulti == leptonMulti::one) + y * (leptonMulti == leptonMulti::two)",
    RooArgList(x, y, leptonMulti));
```

### Debugging a formula that won't compile
When the formula is preprocessed, RooFit can print information in the debug stream.
These can be retrieved by activating the RooFit::MsgLevel `RooFit::DEBUG`
and the RooFit::MsgTopic `RooFit::InputArguments`.
Check the tutorial rf506_msgservice.C for details.
**/

#include "RooFormula.h"
#include "RooAbsReal.h"
#include "RooAbsCategory.h"
#include "RooArgList.h"
#include "RooMsgService.h"
#include "RooBatchCompute.h"

#include "TObjString.h"

#include <memory>
#include <regex>
#include <sstream>
#include <cctype>

using std::sregex_iterator, std::ostream;

namespace {

/// Convert `@i`-style references to `x[i]`.
void convertArobaseReferences(std::string &formula)
{
   bool match = false;
   for (std::size_t i = 0; i < formula.size(); ++i) {
      if (match && !isdigit(formula[i])) {
         formula.insert(formula.begin() + i, ']');
         i += 1;
         match = false;
      } else if (!match && formula[i] == '@') {
         formula[i] = 'x';
         formula.insert(formula.begin() + i + 1, '[');
         i += 1;
         match = true;
      }
   }
   if (match)
      formula += ']';
}

/// Replace all occurrences of `what` with `with` inside of `inOut`.
void replaceAll(std::string &inOut, std::string_view what, std::string_view with)
{
   for (std::string::size_type pos{}; inOut.npos != (pos = inOut.find(what.data(), pos, what.length()));
        pos += with.length()) {
      inOut.replace(pos, what.length(), with.data(), with.length());
   }
}

/// Find the word boundaries with a static std::regex and return a bool vector
/// flagging their positions. The end of the string is considered a word
/// boundary.
std::vector<bool> getWordBoundaryFlags(std::string const &s)
{
   static const std::regex r{"\\b"};
   std::vector<bool> out(s.size() + 1);

   for (auto i = std::sregex_iterator(s.begin(), s.end(), r); i != std::sregex_iterator(); ++i) {
      std::smatch m = *i;
      out[m.position()] = true;
   }

   // The end of a string is also a word boundary
   out[s.size()] = true;

   return out;
}

/// Replace all named references with "x[i]"-style.
void replaceVarNamesWithIndexStyle(std::string &formula, RooArgList const &varList)
{
   std::vector<bool> isWordBoundary = getWordBoundaryFlags(formula);
   for (unsigned int i = 0; i < varList.size(); ++i) {
      std::string_view varName = varList[i].GetName();

      std::stringstream replacementStream;
      replacementStream << "x[" << i << "]";
      std::string replacement = replacementStream.str();

      for (std::string::size_type pos{}; formula.npos != (pos = formula.find(varName.data(), pos, varName.length()));
           pos += replacement.size()) {

         std::string::size_type next = pos + varName.length();

         // The matched variable name has to be surrounded by word boundaries
         // std::cout << pos << "   " << next << std::endl;
         if (!isWordBoundary[pos] || !isWordBoundary[next])
            continue;

         // Veto '[' and ']' as next characters. If the variable is called `x`
         // or `0`, this might otherwise replace `x[0]`.
         if (next < formula.size() && (formula[next] == '[' || formula[next] == ']')) {
            continue;
         }

         // As we replace substrings in the middle of the string, we also have
         // to update the word boundary flag vector. Note that we don't care
         // the word boundaries in the `x[i]` are correct, as it has already
         // been replaced.
         std::size_t nOld = varName.length();
         std::size_t nNew = replacement.size();
         auto wbIter = isWordBoundary.begin() + pos;
         if (nNew > nOld) {
            isWordBoundary.insert(wbIter + nOld, nNew - nOld, false);
         } else if (nNew < nOld) {
            isWordBoundary.erase(wbIter + nNew, wbIter + nOld);
         }

         // Do the actual replacement
         formula.replace(pos, varName.length(), replacement);
      }

      oocxcoutD(static_cast<TObject *>(nullptr), InputArguments)
         << "Preprocessing formula: replace named references: " << varName << " --> " << replacement << "\n\t"
         << formula << std::endl;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// From the internal representation, construct a formula by replacing all index place holders
/// with the names of the variables that are being used to evaluate it, and return it as string.
std::string reconstructFormula(std::string internalRepr, RooArgList const& args) {
  const auto nArgs = args.size();
  for (unsigned int i = 0; i < nArgs; ++i) {
    const auto& var = args[i];
    std::stringstream regexStr;
    regexStr << "x\\[" << i << "\\]|@" << i;
    std::regex regex(regexStr.str());

    std::string replacement = std::string("[") + var.GetName() + "]";
    internalRepr = std::regex_replace(internalRepr, regex, replacement);
  }

  return internalRepr;
}

////////////////////////////////////////////////////////////////////////////////
/// From the internal representation, construct a null-formula by replacing all
/// index place holders with zeroes, and return it as string
std::string reconstructNullFormula(std::string internalRepr, RooArgList const& args) {
  const auto nArgs = args.size();
  for (unsigned int i = 0; i < nArgs; ++i) {
     std::stringstream regexStr;
     regexStr << "x\\[" << i << "\\]|@" << i;
     std::regex regex(regexStr.str());

     std::string replacement = "1e-18";
     internalRepr = std::regex_replace(internalRepr, regex, replacement);
  }

  return internalRepr;
}

}

////////////////////////////////////////////////////////////////////////////////
/// Construct a new formula.
/// \param[in] name Name of the formula.
/// \param[in] formula Formula to be evaluated. Parameters/observables are identified by name
/// or ordinal position in `varList`.
/// \param[in] varList List of variables to be passed to the formula.
/// \param[in] checkVariables Unused parameter.
RooFormula::RooFormula(const char *name, const char *formula, const RooArgList &varList, bool /*checkVariables*/)
   : TNamed(name, formula)
{
   _origList.add(varList);
   _varIsUsed.resize(varList.size());

   installFormulaOrThrow(formula);

   if (_tFormula == nullptr)
      return;

   const std::string processedFormula(_tFormula->GetTitle());

   std::set<unsigned int> matchedOrdinals;
   static const std::regex newOrdinalRegex("\\bx\\[([0-9]+)\\]");
   for (sregex_iterator matchIt = sregex_iterator(processedFormula.begin(), processedFormula.end(), newOrdinalRegex);
        matchIt != sregex_iterator(); ++matchIt) {
      assert(matchIt->size() == 2);
      std::stringstream matchString((*matchIt)[1]);
      unsigned int i;
      matchString >> i;

      matchedOrdinals.insert(i);
   }

   for (unsigned int i : matchedOrdinals) {
      _varIsUsed[i] = true;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Copy constructor
RooFormula::RooFormula(const RooFormula& other, const char* name) :
  TNamed(name ? name : other.GetName(), other.GetTitle()),
  _varIsUsed{other._varIsUsed}
{
  _origList.add(other._origList);

  std::unique_ptr<TFormula> newTF;
  if (other._tFormula) {
    newTF = std::make_unique<TFormula>(*other._tFormula);
    newTF->SetName(GetName());
  }

  _tFormula = std::move(newTF);
}

////////////////////////////////////////////////////////////////////////////////
/// Process given formula by replacing all ordinal and name references by
/// `x[i]`, where `i` matches the position of the argument in `_origList`.
/// Further, references to category states such as `leptonMulti:one` are replaced
/// by the category index.
std::string RooFormula::processFormula(std::string formula) const {

  // WARNING to developers: people use RooFormula a lot via RooGenericPdf and
  // RooFormulaVar! Performance matters here. Avoid non-static std::regex,
  // because constructing these can become a bottleneck because of the regex
  // compilation.

  cxcoutD(InputArguments) << "Preprocessing formula step 1: find category tags (catName::catState) in "
      << formula << std::endl;

  // Step 1: Find all category tags and the corresponding index numbers
  static const std::regex categoryReg("(\\w+)::(\\w+)");
  std::map<std::string, int> categoryStates;
  for (sregex_iterator matchIt = sregex_iterator(formula.begin(), formula.end(), categoryReg);
       matchIt != sregex_iterator(); ++matchIt) {
    assert(matchIt->size() == 3);
    const std::string fullMatch = (*matchIt)[0];
    const std::string catName = (*matchIt)[1];
    const std::string catState = (*matchIt)[2];

    const auto catVariable = dynamic_cast<const RooAbsCategory*>(_origList.find(catName.c_str()));
    if (!catVariable) {
      cxcoutD(InputArguments) << "Formula " << GetName() << " uses '::' to reference a category state as '" << fullMatch
          << "' but a category '" << catName << "' cannot be found in the input variables." << std::endl;
      continue;
    }

    if (!catVariable->hasLabel(catState)) {
      coutE(InputArguments) << "Formula " << GetName() << " uses '::' to reference a category state as '" << fullMatch
          << "' but the category '" << catName << "' does not seem to have the state '" << catState << "'." << std::endl;
      throw std::invalid_argument(formula);
    }
    const int catNum = catVariable->lookupIndex(catState);

    categoryStates[fullMatch] = catNum;
    cxcoutD(InputArguments) << "\n\t" << fullMatch << "\tname=" << catName << "\tstate=" << catState << "=" << catNum;
  }
  cxcoutD(InputArguments) << "-- End of category tags --"<< std::endl;

  // Step 2: Replace all category tags
  for (const auto& catState : categoryStates) {
    replaceAll(formula, catState.first, std::to_string(catState.second));
  }

  cxcoutD(InputArguments) << "Preprocessing formula step 2: replace category tags\n\t" << formula << std::endl;

  // Step 3: Convert `@i`-style references to `x[i]`
  convertArobaseReferences(formula);

  cxcoutD(InputArguments) << "Preprocessing formula step 3: replace '@'-references\n\t" << formula << std::endl;

  // Step 4: Replace all named references with "x[i]"-style
  replaceVarNamesWithIndexStyle(formula, _origList);

  cxcoutD(InputArguments) << "Final formula:\n\t" << formula << std::endl;

  return formula;
}

////////////////////////////////////////////////////////////////////////////////
/// Analyse internal formula to find out which variables are actually in use.
RooArgList RooFormula::usedVariables() const {
  RooArgList useList;

  for (std::size_t i = 0; i < _varIsUsed.size(); ++i) {
    if (_varIsUsed[i]) {
      useList.add(_origList[i]);
    }
  }

  return useList;
}

////////////////////////////////////////////////////////////////////////////////
/// Change used variables to those with the same name in given list.
/// \param[in] newDeps New dependents to replace the old ones.
/// \param[in] mustReplaceAll Will yield an error if one dependent does not have a replacement.
/// \param[in] nameChange Passed down to RooAbsArg::findNewServer(const RooAbsCollection&, bool) const.
bool RooFormula::changeDependents(const RooAbsCollection& newDeps, bool mustReplaceAll, bool nameChange)
{
  //Change current servers to new servers with the same name given in list
  bool errorStat = false;

  // We only consider the usedVariables() for replacement, because only these
  // are registered as servers.
  for (const auto arg : usedVariables()) {
    RooAbsReal* replace = static_cast<RooAbsReal*>(arg->findNewServer(newDeps,nameChange)) ;
    if (replace) {
      _origList.replace(*arg, *replace);

      if (arg->getStringAttribute("origName")) {
        replace->setStringAttribute("origName",arg->getStringAttribute("origName")) ;
      } else {
        replace->setStringAttribute("origName",arg->GetName()) ;
      }

    } else if (mustReplaceAll) {
      coutE(LinkStateMgmt) << __func__ << ": cannot find replacement for " << arg->GetName() << std::endl;
      errorStat = true;
    }
  }

  return errorStat;
}

////////////////////////////////////////////////////////////////////////////////
/// Evaluate the internal TFormula.
///
/// First, all variables serving this instance are evaluated given the normalisation set,
/// and then the formula is evaluated.
/// \param[in] nset Normalisation set passed to evaluation of arguments serving values.
/// \return The result of the evaluation.
double RooFormula::eval(const RooArgSet* nset) const
{
  if (!_tFormula) {
    coutF(Eval) << __func__ << " (" << GetName() << "): Formula didn't compile: " << GetTitle() << std::endl;
    std::string what = "Formula ";
    what += GetTitle();
    what += " didn't compile.";
    throw std::runtime_error(what);
  }

  std::vector<double> pars;
  pars.reserve(_origList.size());
  for (unsigned int i = 0; i < _origList.size(); ++i) {
    if (_origList[i].isCategory()) {
      const auto& cat = static_cast<RooAbsCategory&>(_origList[i]);
      pars.push_back(cat.getCurrentIndex());
    } else {
      const auto& real = static_cast<RooAbsReal&>(_origList[i]);
      pars.push_back(real.getVal(nset));
    }
  }

  return _tFormula->EvalPar(pars.data());
}

void RooFormula::doEval(RooArgList const &actualVars, RooFit::EvalContext &ctx) const
{
   std::span<double> output = ctx.output();

   const int nPars = _origList.size();
   std::vector<std::span<const double>> inputSpans(nPars);
   int iActual = 0;
   for (int i = 0; i < nPars; i++) {
      if(_varIsUsed[i]) {
         std::span<const double> rhs = ctx.at(static_cast<const RooAbsReal *>(&actualVars[iActual]));
         inputSpans[i] = rhs;
         ++iActual;
      }
   }

   std::vector<double> pars(nPars);
   for (size_t i = 0; i < output.size(); i++) {
      for (int j = 0; j < nPars; j++) {
         pars[j] = inputSpans[j].size() > 1 ? inputSpans[j][i] : inputSpans[j][0];
      }
      output[i] = _tFormula->EvalPar(pars.data());
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Printing interface

void RooFormula::printMultiline(ostream& os, Int_t /*contents*/, bool /*verbose*/, TString indent) const
{
  os << indent << "--- RooFormula ---" << std::endl;
  os << indent << " Formula:        '" << GetTitle() << "'" << std::endl;
  os << indent << " Interpretation: '" << reconstructFormula(GetTitle(), _origList) << "'" << std::endl;
  indent.Append("  ");
  os << indent << "Servers: " << _origList << "\n";
  os << indent << "In use : " << actualDependents() << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
/// Check that the formula compiles, and also fulfills the assumptions.
///
void RooFormula::installFormulaOrThrow(const std::string& formula) {
  const std::string processedFormula = processFormula(formula);

  cxcoutD(InputArguments) << "RooFormula '" << GetName() << "' will be compiled as "
      << "\n\t" << processedFormula
      << "\n  and used as"
      << "\n\t" << reconstructFormula(processedFormula, _origList)
      << "\n  with the parameters " << _origList << std::endl;

  auto theFormula = std::make_unique<TFormula>(GetName(), processedFormula.c_str(), /*addToGlobList=*/false);

  if (!theFormula || !theFormula->IsValid()) {
    std::stringstream msg;
    msg << "RooFormula '" << GetName() << "' did not compile or is invalid."
        << "\nInput:\n\t" << formula
        << "\nPassed over to TFormula:\n\t" << processedFormula << std::endl;
    coutF(InputArguments) << msg.str();
    throw std::runtime_error(msg.str());
  }

  if (theFormula && theFormula->GetNdim() != 0) {
    TFormula nullFormula{"nullFormula", reconstructNullFormula(processedFormula, _origList).c_str(), /*addToGlobList=*/false};
    const auto nullDim = nullFormula.GetNdim();
    if (nullDim != 0) {
      // TFormula thinks that we have an n-dimensional formula (n>0), but it shouldn't, as
      // these vars should have been replaced by zeroes in reconstructNullFormula
      // since RooFit only uses the syntax x[0], x[1], x[2], ...
      // This can happen e.g. with variables x,y,z,t that were not supplied in arglist.
      std::stringstream msg;
      msg << "TFormula interprets the formula " << formula << " as " << theFormula->GetNdim()+nullDim << "-dimensional with undefined variable(s) {";
      for (auto i=0; i < nullDim; ++i) {
        msg << nullFormula.GetVarName(i) << ",";
      }
      msg << "}, which could not be supplied by RooFit."
          << "\nThe formula must be modified, or those variables must be supplied in the list of variables." << std::endl;
      coutF(InputArguments) << msg.str();
      throw std::invalid_argument(msg.str());
    }
  }

  _tFormula = std::move(theFormula);
}

/// \endcond
