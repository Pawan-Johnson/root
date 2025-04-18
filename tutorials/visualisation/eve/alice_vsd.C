/// \file
/// \ingroup tutorial_eve
/// Complex example showing ALICE VSD visualization.
///
///  alice_vsd.C - a simple event-display for ALICE
///
///  Only standard ROOT is used to process the ALICE VSD files.
///
///  No ALICE code is needed -- the VSD file is exported from AliRoot into
///  VSD format -- see TEveVSDStructs.h and TEveVSD.h.
///
///  A simple geometry of 10KB, extracted from the full TGeo-geometry, is
///  used to outline the central detectors of ALICE.
///
///  All files are access from the web by using the "CACHEREAD" option.
///
/// \image html eve_alice_vsd.png
/// \macro_code
///
/// \author Matevz Tadel

#include <TEveManager.h>
#include <TEveEventManager.h>
#include <TEveVSD.h>
#include <TEveVSDStructs.h>

#include <TEveTrack.h>
#include <TEveTrackPropagator.h>
#include <TEveGeoShape.h>

#include <TGTab.h>
#include <TGButton.h>

#include <TFile.h>
#include <TKey.h>
#include <TSystem.h>
#include <TPRegexp.h>

// Include components -- compile time link :)

#include "MultiView.C"
MultiView *gMultiView = nullptr;

class TVSDReader {
public:
   // ----------------------------------------------------------
   // File / Event Data
   // ----------------------------------------------------------

   TFile *fFile;
   TDirectory *fDirectory;

   TObjArray *fEvDirKeys;

   TEveVSD *fVSD;

   Int_t fMaxEv, fCurEv;

   // ----------------------------------------------------------
   // Event visualization structures
   // ----------------------------------------------------------

   TEveTrackList *fTrackList;
   TEvePointSet *fITSClusters;
   TEvePointSet *fTPCClusters;
   TEvePointSet *fTRDClusters;
   TEvePointSet *fTOFClusters;

public:
   TVSDReader(const char *file_name)
      : fFile(nullptr),
        fDirectory(nullptr),
        fEvDirKeys(nullptr),
        fVSD(nullptr),

        fMaxEv(-1),
        fCurEv(-1),

        fTrackList(nullptr),
        fITSClusters(nullptr),
        fTPCClusters(nullptr),
        fTRDClusters(nullptr),
        fTOFClusters(nullptr)
   {
      fFile = TFile::Open(file_name);
      if (!fFile) {
         Error("VSD_Reader", "Can not open file '%s' ... terminating.", file_name);
         gSystem->Exit(1);
      }

      fEvDirKeys = new TObjArray;
      TPMERegexp name_re("Event\\d+");
      TObjLink *lnk = fFile->GetListOfKeys()->FirstLink();
      while (lnk) {
         if (name_re.Match(lnk->GetObject()->GetName())) {
            fEvDirKeys->Add(lnk->GetObject());
         }
         lnk = lnk->Next();
      }

      fMaxEv = fEvDirKeys->GetEntriesFast();
      if (fMaxEv == 0) {
         Error("VSD_Reader", "No events to show ... terminating.");
         gSystem->Exit(1);
      }

      fVSD = new TEveVSD;
   }

   virtual ~TVSDReader()
   {
      // Destructor.

      DropEvent();

      delete fVSD;
      delete fEvDirKeys;

      fFile->Close();
      delete fFile;
   }

   void AttachEvent()
   {
      // Attach event data from current directory.

      fVSD->LoadTrees();
      fVSD->SetBranchAddresses();
   }

   void DropEvent()
   {
      // Drup currently held event data, release current directory.

      // Drop old visualization structures.

      gEve->GetViewers()->DeleteAnnotations();
      gEve->GetCurrentEvent()->DestroyElements();

      // Drop old event-data.

      fVSD->DeleteTrees();
      delete fDirectory;
      fDirectory = nullptr;
   }

   //---------------------------------------------------------------------------
   // Event navigation
   //---------------------------------------------------------------------------

   void NextEvent() { GotoEvent(fCurEv + 1); }

   void PrevEvent() { GotoEvent(fCurEv - 1); }

   Bool_t GotoEvent(Int_t ev)
   {
      if (ev < 0 || ev >= fMaxEv) {
         Warning("GotoEvent", "Invalid event id %d.", ev);
         return kFALSE;
      }

      DropEvent();

      // Connect to new event-data.

      fCurEv = ev;
      fDirectory = (TDirectory *)((TKey *)fEvDirKeys->At(fCurEv))->ReadObj();
      fVSD->SetDirectory(fDirectory);

      AttachEvent();

      // Load event data into visualization structures.

      LoadClusters(fITSClusters, "ITS", 0);
      LoadClusters(fTPCClusters, "TPC", 1);
      LoadClusters(fTRDClusters, "TRD", 2);
      LoadClusters(fTOFClusters, "TOF", 3);

      LoadEsdTracks();

      // Fill projected views.

      auto top = gEve->GetCurrentEvent();

      gMultiView->DestroyEventRPhi();
      gMultiView->ImportEventRPhi(top);

      gMultiView->DestroyEventRhoZ();
      gMultiView->ImportEventRhoZ(top);

      gEve->Redraw3D(kFALSE, kTRUE);

      return kTRUE;
   }

   //---------------------------------------------------------------------------
   // Cluster loading
   //---------------------------------------------------------------------------

   void LoadClusters(TEvePointSet *&ps, const TString &det_name, Int_t det_id)
   {
      if (ps == nullptr) {
         ps = new TEvePointSet(det_name);
         ps->SetMainColor((Color_t)(det_id + 2));
         ps->SetMarkerSize(0.5);
         ps->SetMarkerStyle(2);
         ps->IncDenyDestroy();
      } else {
         ps->Reset();
      }

      TEvePointSelector ss(fVSD->fTreeC, ps, "fV.fX:fV.fY:fV.fZ", TString::Format("fDetId==%d", det_id));
      ss.Select();
      ps->SetTitle(TString::Format("N=%d", ps->Size()));

      gEve->AddElement(ps);
   }

   //---------------------------------------------------------------------------
   // Track loading
   //---------------------------------------------------------------------------

   enum ESDTrackFlags {
      kITSin = 0x0001,
      kITSout = 0x0002,
      kITSrefit = 0x0004,
      kITSpid = 0x0008,
      kTPCin = 0x0010,
      kTPCout = 0x0020,
      kTPCrefit = 0x0040,
      kTPCpid = 0x0080,
      kTRDin = 0x0100,
      kTRDout = 0x0200,
      kTRDrefit = 0x0400,
      kTRDpid = 0x0800,
      kTOFin = 0x1000,
      kTOFout = 0x2000,
      kTOFrefit = 0x4000,
      kTOFpid = 0x8000,
      kHMPIDpid = 0x20000,
      kEMCALmatch = 0x40000,
      kTRDbackup = 0x80000,
      kTRDStop = 0x20000000,
      kESDpid = 0x40000000,
      kTIME = 0x80000000
   };

   Bool_t trackIsOn(TEveTrack *t, Int_t mask)
   {
      // Check is track-flag specified by mask are set.

      return (t->GetStatus() & mask) > 0;
   }

   void LoadEsdTracks()
   {
      // Read reconstructed tracks from current event.

      if (fTrackList == nullptr) {
         fTrackList = new TEveTrackList("ESD Tracks");
         fTrackList->SetMainColor(6);
         fTrackList->SetMarkerColor(kYellow);
         fTrackList->SetMarkerStyle(4);
         fTrackList->SetMarkerSize(0.5);

         fTrackList->IncDenyDestroy();
      } else {
         fTrackList->DestroyElements();
      }

      auto trkProp = fTrackList->GetPropagator();
      // !!!! Need to store field on file !!!!
      // Can store TEveMagField ?
      trkProp->SetMagField(0.5);
      trkProp->SetStepper(TEveTrackPropagator::kRungeKutta);

      Int_t nTracks = fVSD->fTreeR->GetEntries();
      for (Int_t n = 0; n < nTracks; ++n) {
         fVSD->fTreeR->GetEntry(n);

         TEveTrack *track = new TEveTrack(&fVSD->fR, trkProp);
         track->SetName(Form("ESD Track %d", fVSD->fR.fIndex));
         track->SetStdTitle();
         track->SetAttLineAttMarker(fTrackList);
         fTrackList->AddElement(track);
      }

      fTrackList->MakeTracks();

      gEve->AddElement(fTrackList);
   }

   ClassDef(TVSDReader, 0);
};

TVSDReader *gVSDReader = nullptr;

// Forward declaration.
void make_gui();

//______________________________________________________________________________
void alice_vsd(const char *vsd_file_name = "http://mtadel.home.cern.ch/mtadel/root/AliVSD.root")
{
   // Main function, initializes the application.
   //
   // 1. Load the auto-generated library holding ESD classes and
   //    ESD dictionaries.
   // 2. Open ESD data-files.
   // 3. Load cartoon geometry.
   // 4. Spawn simple GUI.
   // 5. Load first event.

   TFile::SetCacheFileDir(".");

   TEveVSD::DisableTObjectStreamersForVSDStruct();

   gVSDReader = new TVSDReader(vsd_file_name);

   TEveManager::Create();

   TEveGeoShape *gentle_geom = nullptr;

   { // Simple geometry
      auto geom = TFile::Open("http://mtadel.home.cern.ch/mtadel/root/alice_mini_geom.root", "CACHEREAD");
      if (!geom)
         return;
      auto gse = (TEveGeoShapeExtract *)geom->Get("Gentle");
      gentle_geom = TEveGeoShape::ImportShapeExtract(gse, nullptr);
      geom->Close();
      delete geom;
      gEve->AddGlobalElement(gentle_geom);
   }

   // Standard multi-view
   //=====================

   gMultiView = new MultiView;
   gMultiView->f3DView->GetGLViewer()->SetStyle(TGLRnrCtx::kOutline);

   gMultiView->SetDepth(-10);
   gMultiView->ImportGeomRPhi(gentle_geom);
   gMultiView->ImportGeomRhoZ(gentle_geom);
   gMultiView->SetDepth(0);

   // Final stuff
   //=============

   gEve->GetViewers()->SwitchColorSet();
   gEve->GetDefaultGLViewer()->SetStyle(TGLRnrCtx::kOutline);

   gEve->GetBrowser()->GetTabRight()->SetTab(1);

   make_gui();

   gEve->AddEvent(new TEveEventManager("Event", "ALICE VSD Event"));

   gVSDReader->GotoEvent(0);

   gEve->Redraw3D(kTRUE); // Reset camera after the first event has been shown.
}

//______________________________________________________________________________
void make_gui()
{
   // Create minimal GUI for event navigation.

   auto browser = gEve->GetBrowser();
   browser->StartEmbedding(TRootBrowser::kLeft);

   auto frmMain = new TGMainFrame(gClient->GetRoot(), 1000, 600);
   frmMain->SetWindowName("XX GUI");
   frmMain->SetCleanup(kDeepCleanup);

   auto hf = new TGHorizontalFrame(frmMain);
   {
      TString icondir(TString::Format("%s/icons/", gSystem->Getenv("ROOTSYS")));
      TGPictureButton *b = nullptr;

      b = new TGPictureButton(hf, gClient->GetPicture(icondir + "GoBack.gif"));
      hf->AddFrame(b);
      b->Connect("Clicked()", "TVSDReader", gVSDReader, "PrevEvent()");

      b = new TGPictureButton(hf, gClient->GetPicture(icondir + "GoForward.gif"));
      hf->AddFrame(b);
      b->Connect("Clicked()", "TVSDReader", gVSDReader, "NextEvent()");
   }
   frmMain->AddFrame(hf);

   frmMain->MapSubwindows();
   frmMain->Resize();
   frmMain->MapWindow();

   browser->StopEmbedding();
   browser->SetTabTitle("Event Control", 0);
}
