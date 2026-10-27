#include "MainFrame.h"

#include <wx/wx.h>

class ModbusGuiApp : public wxApp {
public:
    bool OnInit() override
    {
        if (!wxApp::OnInit())
            return false;
        SetAppName("modbus_gui");
        SetAppDisplayName("Modbus GUI");
        wxImage::AddHandler(new wxPNGHandler);
        auto* frame = new MainFrame;
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(ModbusGuiApp);
