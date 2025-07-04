# -*- coding:utf-8 -*-

# -----------------------------------------------------------------------------
#  Author: Danilo Piparo <Danilo.Piparo@cern.ch> CERN
# -----------------------------------------------------------------------------

################################################################################
# Copyright (C) 1995-2020, Rene Brun and Fons Rademakers.                      #
# All rights reserved.                                                         #
#                                                                              #
# For the licensing terms see $ROOTSYS/LICENSE.                                #
# For the list of contributors see $ROOTSYS/README/CREDITS.                    #
################################################################################


from __future__ import print_function

import fnmatch
import os
import re
import sys
import tempfile
import time
from contextlib import contextmanager
from datetime import datetime
from hashlib import sha1
from subprocess import check_output

import ROOT
from IPython import display, get_ipython
from IPython.core.extensions import ExtensionManager

from JupyROOT.helpers import handlers

# We want iPython to take over the graphics
ROOT.gROOT.SetBatch()

cppMIME = "text/x-c++src"

# Keep display handle for canvases to be able update them
_canvasHandles = {}

_jsMagicHighlight = """
Jupyter.CodeCell.options_default.highlight_modes['magic_{cppMIME}'] = {{'reg':[/^%%cpp/]}};
console.log("JupyROOT - %%cpp magic configured");
"""

_jsNotDrawableClassesPatterns = ["TEve*"]

_jsCanvasWidth = 800
_jsCanvasHeight = 600
_jsCode = """

<div id="{jsDivId}" style="width: {jsCanvasWidth}px; height: {jsCanvasHeight}px; position: relative">
</div>

<script>

function display_{jsDivId}(Core) {{
   let obj = Core.parse({jsonContent});
   Core.settings.HandleKeys = false;
   Core.draw("{jsDivId}", obj, "{jsDrawOptions}");
}}

function script_load_{jsDivId}(src, on_error) {{
    let script = document.createElement('script');
    script.src = src;
    script.onload = function() {{ display_{jsDivId}(JSROOT); }};
    script.onerror = function() {{ script.remove(); on_error(); }};
    document.head.appendChild(script);
}}

if (typeof requirejs !== 'undefined') {{

    // We are in jupyter notebooks, use require.js which should be configured already
    requirejs.config({{
       paths: {{ 'JSRootCore' : [ 'build/jsroot', 'https://root.cern/js/7.7.4/build/jsroot', 'https://jsroot.gsi.de/7.7.4/build/jsroot' ] }}
    }})(['JSRootCore'],  function(Core) {{
       display_{jsDivId}(Core);
    }});

}} else if (typeof JSROOT !== 'undefined') {{

   // JSROOT already loaded, just use it
   display_{jsDivId}(JSROOT);

}} else {{

    // We are in jupyterlab without require.js, directly loading jsroot
    // Jupyterlab might be installed in a different base_url so we need to know it.
    try {{
        var base_url = JSON.parse(document.getElementById('jupyter-config-data').innerHTML).baseUrl;
    }} catch(_) {{
        var base_url = '/';
    }}

    // Try loading a local version of requirejs and fallback to cdn if not possible.
    script_load_{jsDivId}(base_url + 'static/build/jsroot.js', function(){{
        console.error('Fail to load JSROOT locally, please check your jupyter_notebook_config.py file');
        script_load_{jsDivId}('https://root.cern/js/7.7.4/build/jsroot.js', function(){{
            document.getElementById("{jsDivId}").innerHTML = "Failed to load JSROOT";
        }});
    }});
}}

</script>
"""

TBufferJSONErrorMessage = "The TBufferJSON class is necessary for JS visualisation to work and cannot be found. Did you enable the http module (-D http=ON for CMake)?"


def TBufferJSONAvailable():
    if hasattr(ROOT, "TBufferJSON"):
        return True
    print(TBufferJSONErrorMessage, file=sys.stderr)
    return False


def TWebCanvasAvailable():
    if hasattr(ROOT, "TWebCanvas"):
        return True
    return False


def RCanvasAvailable():
    if not hasattr(ROOT, "Experimental"):
        return False
    if not hasattr(ROOT.Experimental, "RCanvas"):
        return False
    return True


def _initializeJSVis():
    global _enableJSVis
    jupyter_jsroot = ROOT.gEnv.GetValue("Jupyter.JSRoot", "on").lower()
    if jupyter_jsroot not in {"on", "off"}:
        print(f"Invalid Jupyter.JSRoot value '{jupyter_jsroot}' in .rootrc. Using default 'on'.")
        jupyter_jsroot = "on"
    _enableJSVis = jupyter_jsroot == "on"


_initializeJSVis()
_enableJSVisDebug = False


def enableJSVis():
    if not TBufferJSONAvailable():
        return
    global _enableJSVis
    _enableJSVis = True


def disableJSVis():
    global _enableJSVis
    _enableJSVis = False


def enableJSVisDebug():
    if not TBufferJSONAvailable():
        return
    global _enableJSVis
    global _enableJSVisDebug
    _enableJSVis = True
    _enableJSVisDebug = True


def disableJSVisDebug():
    global _enableJSVis
    global _enableJSVisDebug
    _enableJSVis = False
    _enableJSVisDebug = False


def _getPlatform():
    return sys.platform


def _getLibExtension(thePlatform):
    """Return appropriate file extension for a shared library
    >>> _getLibExtension('darwin')
    '.dylib'
    >>> _getLibExtension('win32')
    '.dll'
    >>> _getLibExtension('OddPlatform')
    '.so'
    """
    pExtMap = {"darwin": ".dylib", "win32": ".dll"}
    return pExtMap.get(thePlatform, ".so")


@contextmanager
def _setIgnoreLevel(level):
    originalLevel = ROOT.gErrorIgnoreLevel
    ROOT.gErrorIgnoreLevel = level
    yield
    ROOT.gErrorIgnoreLevel = originalLevel


def commentRemover(text):
    """
    >>> s="// hello"
    >>> commentRemover(s)
    ''
    >>> s="int /** Test **/ main() {return 0;}"
    >>> commentRemover(s)
    'int  main() {return 0;}'
    """

    def blotOutNonNewlines(strIn):  # Return a string containing only the newline chars contained in strIn
        return "" + ("\n" * strIn.count("\n"))

    def replacer(match):
        s = match.group(0)
        if s.startswith("/"):  # Matched string is //...EOL or /*...*/  ==> Blot out all non-newline chars
            return blotOutNonNewlines(s)
        else:  # Matched string is '...' or "..."  ==> Keep unchanged
            return s

    pattern = re.compile(r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\\\'])*\'|"(?:\\.|[^\\"])*"', re.DOTALL | re.MULTILINE)

    return re.sub(pattern, replacer, text)


# Here functions are defined to process C++ code
def processCppCodeImpl(code):
    # code = commentRemover(code)
    ROOT.gInterpreter.ProcessLine(code)


def processMagicCppCodeImpl(code):
    err = ROOT.ProcessLineWrapper(code)
    if err == ROOT.TInterpreter.kProcessing:
        ROOT.gInterpreter.ProcessLine(".@")
        ROOT.gInterpreter.ProcessLine('cerr << "Unbalanced braces. This cell was not processed." << endl;')


def declareCppCodeImpl(code):
    # code = commentRemover(code)
    ROOT.gInterpreter.Declare(code)


def processCppCode(code):
    processCppCodeImpl(code)


def processMagicCppCode(code):
    processMagicCppCodeImpl(code)


def declareCppCode(code):
    declareCppCodeImpl(code)


def _checkOutput(command, errMsg=None):
    out = ""
    try:
        out = check_output(command.split())
    except Exception:
        if errMsg:
            sys.stderr.write("%s (command was %s)\n" % (errMsg, command))
    return out


def _invokeAclicMac(fileName):
    """FIXME!
    This function is a workaround. On osx, it is impossible to link against
    libzmq.so, among the others. The error is known and is
    "ld: can't link with bundle (MH_BUNDLE) only dylibs (MH_DYLIB)"
    We cannot at the moment force Aclic to change the linker command in order
    to exclude these libraries, so we launch a second root session to compile
    the library, which we then load.
    """
    command = 'root -l -q -b -e gSystem->CompileMacro("%s","k")*0' % fileName
    out = _checkOutput(command, "Error ivoking ACLiC")  # noqa: F841
    libNameBase = fileName.replace(".C", "_C")
    ROOT.gSystem.Load(libNameBase)


def _codeToFilename(code):
    """Convert code to a unique file name

    >>> code = "int f(i){return i*i;}"
    >>> _codeToFilename(code)[0:9]
    'dbf7e731_'
    >>> _codeToFilename(code)[9:-2].isdigit()
    True
    >>> _codeToFilename(code)[-2:]
    '.C'
    """
    code_enc = code if type(code) is bytes else code.encode("utf-8")
    fileNameBase = sha1(code_enc).hexdigest()[0:8]
    timestamp = datetime.now().strftime("%H%M%S%f")
    return fileNameBase + "_" + timestamp + ".C"


def _dumpToUniqueFile(code):
    """Dump code to file whose name is unique

    >>> code = "int f(i){return i*i;}"
    >>> _dumpToUniqueFile(code)[0:9]
    'dbf7e731_'
    >>> _dumpToUniqueFile(code)[9:-2].isdigit()
    True
    >>> _dumpToUniqueFile(code)[-2:]
    '.C'
    """
    fileName = _codeToFilename(code)
    with open(fileName, "w") as ofile:
        code_dec = code if type(code) is not bytes else code.decode("utf-8")
        ofile.write(code_dec)
    return fileName


def isPlatformApple():
    return _getPlatform() == "darwin"


def invokeAclic(cell):
    fileName = _dumpToUniqueFile(cell)
    if isPlatformApple():
        _invokeAclicMac(fileName)
    else:
        processCppCode(".L %s+" % fileName)


def produceCanvasJson(canvas):
    if canvas.IsUpdated() and not canvas.IsDrawn():
        canvas.Draw()

    if TWebCanvasAvailable():
        return ROOT.TWebCanvas.CreateCanvasJSON(canvas, 23, True)

    # Add extra primitives to canvas with custom colors, palette, gStyle

    prim = canvas.GetListOfPrimitives()

    style = ROOT.gStyle
    colors = ROOT.gROOT.GetListOfColors()
    palette = None

    # always provide gStyle object
    if prim.FindObject(style):
        style = None
    else:
        prim.Add(style)

    cnt = 0
    for n in range(colors.GetLast() + 1):
        if colors.At(n):
            cnt = cnt + 1

    # add all colors if there are more than 598 colors defined
    if cnt < 599 or prim.FindObject(colors):
        colors = None
    else:
        prim.Add(colors)

    if colors:
        pal = ROOT.TColor.GetPalette()
        palette = ROOT.TObjArray()
        palette.SetName("CurrentColorPalette")
        for i in range(pal.GetSize()):
            palette.Add(colors.At(pal[i]))
        prim.Add(palette)

    ROOT.TColor.DefinedColors()

    canvas_json = ROOT.TBufferJSON.ConvertToJSON(canvas, 23)

    # Cleanup primitives after conversion
    if style is not None:
        prim.Remove(style)
    if colors is not None:
        prim.Remove(colors)
    if palette is not None:
        prim.Remove(palette)

    return canvas_json


transformers = []


class StreamCapture(object):
    def __init__(self, ip=get_ipython()):
        # For the registration
        self.shell = ip

        self.ioHandler = handlers.IOHandler()
        self.flag = True
        self.outString = ""
        self.errString = ""

        self.poller = handlers.Poller()
        self.poller.start()
        self.asyncCapturer = handlers.Runner(self.syncCapture, self.poller)

        self.isFirstPreExecute = True
        self.isFirstPostExecute = True

    def syncCapture(self, defout=""):
        self.outString = defout
        self.errString = defout
        waitTimes = [0.01, 0.01, 0.02, 0.04, 0.06, 0.08, 0.1]
        lenWaitTimes = 7

        iterIndex = 0
        while self.flag:
            self.ioHandler.Poll()
            if not self.flag:
                return
            waitTime = 0.1 if iterIndex >= lenWaitTimes else waitTimes[iterIndex]
            time.sleep(waitTime)

    def pre_execute(self):
        if self.isFirstPreExecute:
            self.isFirstPreExecute = False
            return 0

        self.flag = True
        self.ioHandler.Clear()
        self.ioHandler.InitCapture()
        self.asyncCapturer.AsyncRun("")

    def post_execute(self):
        if self.isFirstPostExecute:
            self.isFirstPostExecute = False
            self.isFirstPreExecute = False
            return 0
        self.flag = False
        self.asyncCapturer.Wait()
        self.ioHandler.Poll()
        self.ioHandler.EndCapture()

        # Print for the notebook
        out = self.ioHandler.GetStdout()
        err = self.ioHandler.GetStderr()
        if not transformers:
            sys.stdout.write(out)
            sys.stderr.write(err)
        else:
            for t in transformers:
                (out, err, otype) = t(out, err)
                if otype == "html":
                    display.display(display.HTML(out))
                    display.display(display.HTML(err))
        return 0

    def register(self):
        self.shell.events.register("pre_execute", self.pre_execute)
        self.shell.events.register("post_execute", self.post_execute)

    def __del__(self):
        self.poller.Stop()


def GetCanvasDrawers():
    lOfC = ROOT.gROOT.GetListOfCanvases()
    return [NotebookDrawer(can) for can in lOfC if can.IsDrawn() or can.IsUpdated()]


def GetRCanvasDrawers():
    if not RCanvasAvailable():
        return []
    lOfC = ROOT.Experimental.RCanvas.GetCanvases()
    return [NotebookDrawer(can.__smartptr__().get()) for can in lOfC if can.IsShown() or can.IsUpdated()]


def GetGeometryDrawer():
    if not hasattr(ROOT, "gGeoManager"):
        return
    if not ROOT.gGeoManager:
        return
    if not ROOT.gGeoManager.GetUserPaintVolume():
        return
    vol = ROOT.gGeoManager.GetTopVolume()
    if vol:
        return NotebookDrawer(vol)


def GetDrawers():
    drawers = GetCanvasDrawers() + GetRCanvasDrawers()
    geometryDrawer = GetGeometryDrawer()
    if geometryDrawer:
        drawers.append(geometryDrawer)
    return drawers


def DrawGeometry():
    drawer = GetGeometryDrawer()
    if drawer:
        drawer.Draw()


def DrawCanvases():
    drawers = GetCanvasDrawers()
    for drawer in drawers:
        drawer.Draw()


def DrawRCanvases():
    rdrawers = GetRCanvasDrawers()
    for drawer in rdrawers:
        drawer.Draw()


def NotebookDraw():
    DrawGeometry()
    DrawCanvases()
    DrawRCanvases()


class CaptureDrawnPrimitives(object):
    """
    Capture the canvas which is drawn to display it.
    """

    def __init__(self, ip=get_ipython()):
        self.shell = ip

    def _post_execute(self):
        NotebookDraw()

    def register(self):
        self.shell.events.register("post_execute", self._post_execute)


class NotebookDrawer(object):
    """
    Capture the canvas which is drawn and decide if it should be displayed using
    jsROOT.
    """

    def __init__(self, theObject):
        self.drawableObject = theObject
        self.isRCanvas = False
        self.isCanvas = False
        self.drawableId = str(ROOT.AddressOf(theObject)[0])
        if hasattr(self.drawableObject, "ResolveSharedPtrs"):
            self.isRCanvas = True
        else:
            self.isCanvas = self.drawableObject.ClassName() == "TCanvas"

    def __del__(self):
        if self.isRCanvas:
            self.drawableObject.ClearShown()
            self.drawableObject.ClearUpdated()
        elif self.isCanvas:
            self.drawableObject.ResetDrawn()
            self.drawableObject.ResetUpdated()
        else:
            ROOT.gGeoManager.SetUserPaintVolume(None)

    def _getListOfPrimitivesNamesAndTypes(self):
        """
        Get the list of primitives in the pad, recursively descending into
        histograms and graphs looking for fitted functions.
        """
        primitives = self.drawableObject.GetListOfPrimitives()
        primitivesNames = map(lambda p: p.ClassName(), primitives)
        return sorted(primitivesNames)

    def _getUniqueDivId(self):
        """
        Every DIV containing a JavaScript snippet must be unique in the
        notebook. This method provides a unique identifier.
        With the introduction of JupyterLab, multiple Notebooks can exist
        simultaneously on the same HTML page. In order to ensure a unique
        identifier with the UID throughout all open Notebooks the UID is
        generated as a timestamp.
        """
        return "root_plot_" + str(int(round(time.time() * 1000)))

    def _canJsDisplay(self):
        # returns true if js-based drawing should be used
        if not TBufferJSONAvailable():
            return False
        # RCanvas clways displayed with jsroot
        if self.isRCanvas:
            return True
        # check if jsroot was disabled
        if not _enableJSVis:
            return False
        # geometry can be drawn, TWebCanvas also can be used
        if not self.isCanvas or TWebCanvasAvailable():
            return True

        # to be optimised
        primitivesTypesNames = self._getListOfPrimitivesNamesAndTypes()
        for unsupportedPattern in _jsNotDrawableClassesPatterns:
            for primitiveTypeName in primitivesTypesNames:
                if fnmatch.fnmatch(primitiveTypeName, unsupportedPattern):
                    print(
                        "The canvas contains an object of a type jsROOT cannot currently handle (%s). Falling back to a static png."
                        % primitiveTypeName,
                        file=sys.stderr,
                    )
                    return False
        return True

    def _getJsCode(self):
        # produce JSON for the canvas
        if self.isRCanvas:
            json = self.drawableObject.CreateJSON()
        else:
            json = produceCanvasJson(self.drawableObject).Data()

        divId = self._getUniqueDivId()

        width = _jsCanvasWidth
        height = _jsCanvasHeight
        options = "all"

        if self.isCanvas:
            if self.drawableObject.GetWindowWidth() > 0:
                width = self.drawableObject.GetWindowWidth()
            if self.drawableObject.GetWindowHeight() > 0:
                height = self.drawableObject.GetWindowHeight()
            options = ""

        if self.isRCanvas:
            if self.drawableObject.GetWidth() > 0:
                width = self.drawableObject.GetWidth()
            if self.drawableObject.GetHeight() > 0:
                height = self.drawableObject.GetHeight()
            options = ""

        thisJsCode = _jsCode.format(
            jsCanvasWidth=width, jsCanvasHeight=height, jsonContent=json, jsDrawOptions=options, jsDivId=divId
        )
        return thisJsCode

    def _getJsDiv(self):
        return display.HTML(self._getJsCode())

    def _getDrawId(self):
        if self.isCanvas:
            return self.drawableObject.GetName() + self.drawableId
        if self.isRCanvas:
            return self.drawableObject.GetUID()
        # all other objects do not support update and can be ignored
        return ""

    def _getUpdated(self):
        if self.isCanvas:
            return self.drawableObject.IsUpdated()
        if self.isRCanvas:
            return self.drawableObject.IsUpdated()
        return False

    def _jsDisplay(self):
        global _canvasHandles
        name = self._getDrawId()
        updated = self._getUpdated()
        jsdiv = self._getJsDiv()
        if name and (name in _canvasHandles) and updated:
            _canvasHandles[name].update(jsdiv)
        elif name:
            _canvasHandles[name] = display.display(jsdiv, display_id=True)
        else:
            display.display(jsdiv)
        return 0

    def _getPngImage(self):
        ofile = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
        with _setIgnoreLevel(ROOT.kError):
            self.drawableObject.SaveAs(ofile.name)
        img = display.Image(filename=ofile.name, format="png", embed=True)
        ofile.close()
        os.unlink(ofile.name)
        return img

    def _pngDisplay(self):
        global _canvasHandles
        name = self._getDrawId()
        updated = self._getUpdated()
        img = self._getPngImage()
        if updated and name and (name in _canvasHandles):
            _canvasHandles[name].update(img)
        elif name:
            _canvasHandles[name] = display.display(img, display_id=True)
        else:
            display.display(img)

    def _display(self):
        if _enableJSVisDebug:
            self._pngDisplay()
            self._jsDisplay()
        else:
            if self._canJsDisplay():
                self._jsDisplay()
            else:
                self._pngDisplay()

    def GetDrawableObjects(self):
        if _enableJSVisDebug:
            return [self._getJsDiv(), self._getPngImage()]

        if self._canJsDisplay():
            return [self._getJsDiv()]
        else:
            return [self._getPngImage()]

    def Draw(self):
        self._display()
        return 0


def setStyle():
    style = ROOT.gStyle
    style.SetFuncWidth(2)


captures = []


def loadMagicsAndCapturers():
    global captures
    extNames = ["JupyROOT.magics." + name for name in ["cppmagic", "jsrootmagic"]]
    ip = get_ipython()
    extMgr = ExtensionManager(ip)
    for extName in extNames:
        extMgr.load_extension(extName)
    captures.append(StreamCapture())
    captures.append(CaptureDrawnPrimitives())

    for capture in captures:
        capture.register()


def declareProcessLineWrapper():
    ROOT.gInterpreter.Declare("""
TInterpreter::EErrorCode ProcessLineWrapper(const char* line) {
    TInterpreter::EErrorCode err;
    gInterpreter->ProcessLine(line, &err);
    return err;
}
""")


def enhanceROOTModule():
    ROOT.enableJSVis = enableJSVis
    ROOT.disableJSVis = disableJSVis
    ROOT.enableJSVisDebug = enableJSVisDebug
    ROOT.disableJSVisDebug = disableJSVisDebug


def enableCppHighlighting():
    ipDispJs = display.display_javascript
    # Define highlight mode for %%cpp magic
    ipDispJs(_jsMagicHighlight.format(cppMIME=cppMIME), raw=True)


def iPythonize():
    setStyle()
    loadMagicsAndCapturers()
    declareProcessLineWrapper()
    # enableCppHighlighting()
    enhanceROOTModule()
