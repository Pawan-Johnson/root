/// \file
/// \ingroup tutorial_eve
/// How to use EVE without the standard window.
/// Type
///   `gEve->GetBrowser()->MapWindow()`
/// to bring it up for object interaction, debugging, etc.
///
/// \image html eve_arrow_standalone.png
/// \macro_code
///
/// \author Matevz Tadel

void arrow_standalone()
{
   TEveManager::Create(kFALSE);

   // ----------------------------------------------------------------------

   auto mf = new TGMainFrame(gClient->GetRoot(), 800, 400, kHorizontalFrame);
   mf->SetWindowName("Arrow Foo");

   // ----------------------------------------------------------------------

   auto evf = new TGCompositeFrame(mf, 400, 400);
   mf->AddFrame(evf, new TGLayoutHints(kLHintsNormal | kLHintsExpandX | kLHintsExpandY));

   auto ev = new TGLEmbeddedViewer(evf);
   evf->AddFrame(ev->GetFrame(), new TGLayoutHints(kLHintsNormal | kLHintsExpandX | kLHintsExpandY));

   auto eve_v = new TEveViewer("YourViewer");
   eve_v->SetGLViewer(ev, ev->GetFrame());
   eve_v->IncDenyDestroy();
   eve_v->AddScene(gEve->GetEventScene());
   gEve->GetViewers()->AddElement(eve_v);

   // ----------------------------------------------------------------------

   // To create embedded canvas ... no menus on top.

   // TRootEmbeddedCanvas* ec =
   //    new TRootEmbeddedCanvas("EmbeddedCanvas", mf, 400, 400);
   // mf->AddFrame(ec, new TGLayoutHints(kLHintsNormal  |
   //                                    kLHintsExpandX | kLHintsExpandY));

   // --------------------------------

   // This one is tricky - must be after embedded canvas but before std canvas!
   mf->MapSubwindows();

   // --------------------------------

   // To create full canvas with menus.

   mf->SetEditable();
   auto c = new TCanvas("Foo", "Bar", 400, 400);
   mf->SetEditable(kFALSE);

   // ----------------------------------------------------------------------

   mf->Layout();
   mf->MapWindow();

   // ----------------------------------------------------------------------

   // Populate the viewer ... here we just call the arrow.C.

   TEveUtil::Macro("arrow.C");
}
