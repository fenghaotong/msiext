#include "StdAfx.h"
#include "TemplateFiles.h"

enum ExecuteAttributes
{
    ExecuteOnInstall = 1,
    ExecuteOnUnInstall = 2,
    // ExecuteOnRollback = 4,
    // ExecuteOnReInstall = 8,
};

CA_API UINT __stdcall TemplateFiles_Immediate(MSIHANDLE hInstall)
{
	MSI_EXCEPTION_HANDLER_PROLOG;
    MsiInstall msiInstall(hInstall);

	// combined xml document
	AppSecInc::Xml::XmlDocument combined_xml_document;
	combined_xml_document.Create();
	MSXML2::IXMLDOMNodePtr combined_xml_root = combined_xml_document.AppendChild(L"TemplateFiles");

    std::wstring xml = msiInstall.GetViewData(L"SELECT * FROM `TemplateFiles`");
    AppSecInc::Xml::XmlDocument xmlDocument;
    xmlDocument.LoadXml(xml);

    {
        MSXML2::IXMLDOMNodeListPtr rows = xmlDocument.SelectNodes(L"//Row");
        MSXML2::IXMLDOMNodePtr row = NULL;
        while (NULL != (row = rows->nextNode()))
        {
            // id
		    std::wstring templatefile_id = xmlDocument.SelectNodeValue(L"Data[@Column=\"Id\"]", row, L"");
            // component id
		    std::wstring component_id = xmlDocument.SelectNodeValue(L"Data[@Column=\"ComponentId\"]", row, L"");
            // node condition
            std::wstring condition = xmlDocument.SelectNodeValue(L"Data[@Column=\"Condition\"]", row);
            // operational attributes
            long attributes = AppSecInc::StringUtils::stringToLong(xmlDocument.SelectNodeValue(L"Data[@Column=\"Attributes\"]", row));
            // no condition (executes by default) or condition evaluates to true
            bool execute_per_condition = condition.empty() || msiInstall.EvaluateCondition(condition);
            if (! condition.empty())
            {
                // set the evaluated value for debugging purposes
                xmlDocument.SelectNode(L"Data[@Column=\"Condition\"]", row)->text = _bstr_t(execute_per_condition ? L"1" : L"0");
            }
            // execute on install
            bool execute_per_component_install = (component_id.empty() || msiInstall.IsComponentInstalling(component_id));
            // execute on uninstall
            bool execute_per_component_uninstall = (component_id.empty() || msiInstall.IsComponentUnInstalling(component_id));

            bool execute = execute_per_condition && (
                (execute_per_component_install && (attributes & ExecuteOnInstall) && msiInstall.IsInstalling()) 
                || (execute_per_component_uninstall && (attributes & ExecuteOnUnInstall) && msiInstall.IsUnInstalling())
                );

		    MSXML2::IXMLDOMNodePtr templatefile_node = combined_xml_document.AppendChild(L"TemplateFile", combined_xml_root);
		    combined_xml_document.SetAttribute(L"id", templatefile_id, templatefile_node);
            std::wstring source = xmlDocument.SelectNodeValue(L"Data[@Column=\"Source\"]", row);
            std::wstring target = xmlDocument.SelectNodeValue(L"Data[@Column=\"Target\"]", row, source);
		    combined_xml_document.AppendChild(L"Source", templatefile_node)->text = _bstr_t(source.c_str());
		    combined_xml_document.AppendChild(L"Target", templatefile_node)->text = _bstr_t(target.c_str());
            combined_xml_document.SetAttribute(L"execute", execute ? L"true" : L"false", templatefile_node);

		    MSXML2::IXMLDOMNodePtr properties_node = combined_xml_document.AppendChild(L"Properties", templatefile_node);

            // append built-in properties
            {
                AppSecInc::Xml::XmlDocument xmlPropertiesDocument;
                xmlPropertiesDocument.LoadXml(msiInstall.GetViewData(L"SELECT * FROM `Property`"));
                MSXML2::IXMLDOMNodeListPtr property_rows = xmlPropertiesDocument.SelectNodes(L"//Row");
                MSXML2::IXMLDOMNodePtr property_row = NULL;
                while (NULL != (property_row = property_rows->nextNode()))
                {
		            std::wstring name = xmlPropertiesDocument.SelectNodeValue(L"Data[@Column=\"Property\"]", property_row);
		            std::wstring value = xmlPropertiesDocument.SelectNodeValue(L"Data[@Column=\"Value\"]", property_row);
                    MSXML2::IXMLDOMNodePtr property_node = combined_xml_document.AppendChild(L"Property", properties_node);
                    combined_xml_document.SetAttribute(L"name", name, property_node);
                    combined_xml_document.SetAttribute(L"value", value, property_node);
                }
            }

            // append properties from this TemplateFile
            {
                AppSecInc::Xml::XmlDocument xmlPropertiesDocument;
                xmlPropertiesDocument.LoadXml(msiInstall.GetViewData(L"SELECT * FROM `TemplateFileProperties`"));
                MSXML2::IXMLDOMNodeListPtr property_rows = xmlPropertiesDocument.SelectNodes(L"//Row");
                MSXML2::IXMLDOMNodePtr property_row = NULL;
                while (NULL != (property_row = property_rows->nextNode()))
                {
			        // \todo Change XPATH to fetch only rows that match ID
			        std::wstring id = xmlPropertiesDocument.SelectNodeValue(L"Data[@Column=\"TemplateFileId\"]", property_row);
			        if (id != templatefile_id)
				        continue;

		            std::wstring name = xmlPropertiesDocument.SelectNodeValue(L"Data[@Column=\"Name\"]", property_row);
		            std::wstring value = xmlPropertiesDocument.SelectNodeValue(L"Data[@Column=\"Value\"]", property_row);
                    MSXML2::IXMLDOMNodePtr property_node = combined_xml_document.AppendChild(L"Property", properties_node);
                    combined_xml_document.SetAttribute(L"name", name, property_node);
                    combined_xml_document.SetAttribute(L"value", value, property_node);
		        }
            }
        }
    }

    msiInstall.SetActionData(L"TemplateFiles_Deferred", combined_xml_document.GetXml());

	MSI_EXCEPTION_HANDLER_EPILOG;
    return ERROR_SUCCESS;
}

CA_API UINT __stdcall TemplateFiles_Rollback(MSIHANDLE hInstall)
{
    // \todo execute rollback actions
    return ERROR_NOT_SUPPORTED;
}

CA_API UINT __stdcall TemplateFiles_Deferred(MSIHANDLE hInstall)
{
	MSI_EXCEPTION_HANDLER_PROLOG;
    MsiInstall msiInstall(hInstall);

    AppSecInc::Xml::XmlDocument xmlDocument;
    xmlDocument.LoadXml(msiInstall.GetActionData());

    MSXML2::IXMLDOMNodeListPtr rows = xmlDocument.SelectNodes(L"//TemplateFile[@execute='true']"); // \todo //Row[@rollback='false']
    MSXML2::IXMLDOMNodePtr row = NULL;
    while (NULL != (row = rows->nextNode()))
    {
        std::wstring id = xmlDocument.SelectAttributeValue(L"id", row);
        std::wstring source = xmlDocument.SelectNodeValue(L"Source", row);
        std::wstring target = xmlDocument.SelectNodeValue(L"Target", row, source);

        std::map<std::wstring, std::wstring> properties;

        {
            MSXML2::IXMLDOMNodeListPtr property_rows = xmlDocument.SelectNodes(L"Properties/Property", row);
            MSXML2::IXMLDOMNodePtr property_row = NULL;
            while (NULL != (property_row = property_rows->nextNode()))
            {
                std::wstring name = xmlDocument.SelectAttributeValue(L"name", property_row);
                std::wstring value = xmlDocument.SelectAttributeValue(L"value", property_row);
                properties[name] = value;
            }
        }

        std::wstring data;
        AppSecInc::File::ReadToEnd(source, data);
        data = AppSecInc::Formatter::FormatTemplate(data, properties);

        std::string char_data = AppSecInc::StringUtils::wc2mb(data);
        std::vector<char> binary_data;
        binary_data.assign(char_data.begin(), char_data.end());

        AppSecInc::File::FileWrite(target, binary_data);
    }

	MSI_EXCEPTION_HANDLER_EPILOG;
    return ERROR_SUCCESS;
}