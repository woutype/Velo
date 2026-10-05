#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Thin wrapper over PropertiesFile (%APPDATA%/Velo/Velo.settings). Message-thread only.
class Settings
{
public:
    Settings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Velo";
        o.filenameSuffix = ".settings";
        o.folderName = "Velo";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        file = std::make_unique<juce::PropertiesFile> (o);
    }

    ~Settings() { save(); }

    static juce::File getDataDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Velo");
        d.createDirectory();
        return d;
    }

    bool hasKey (const juce::String& k) const              { return file->containsKey (k); }
    juce::String getString (const juce::String& k, const juce::String& d = {}) const { return file->getValue (k, d); }
    int    getInt  (const juce::String& k, int d = 0) const          { return file->getIntValue (k, d); }
    double getDouble (const juce::String& k, double d = 0.0) const   { return file->getDoubleValue (k, d); }
    bool   getBool (const juce::String& k, bool d = false) const     { return file->getBoolValue (k, d); }

    void setString (const juce::String& k, const juce::String& v) { file->setValue (k, v); }
    void setInt (const juce::String& k, int v)                    { file->setValue (k, v); }
    void setDouble (const juce::String& k, double v)              { file->setValue (k, v); }
    void setBool (const juce::String& k, bool v)                  { file->setValue (k, v); }

    juce::StringArray getList (const juce::String& k) const
    {
        juce::StringArray a;
        a.addLines (file->getValue (k));
        a.removeEmptyStrings();
        return a;
    }
    void setList (const juce::String& k, const juce::StringArray& a) { file->setValue (k, a.joinIntoString ("\n")); }

    std::unique_ptr<juce::XmlElement> getXml (const juce::String& k) const { return file->getXmlValue (k); }
    void setXml (const juce::String& k, const juce::XmlElement* xml)
    {
        if (xml != nullptr) file->setValue (k, xml); else file->removeValue (k);
    }

    void remove (const juce::String& k) { file->removeValue (k); }
    void save() { if (file != nullptr) file->saveIfNeeded(); }

private:
    std::unique_ptr<juce::PropertiesFile> file;
};
