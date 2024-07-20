﻿using System;
using EpicGames.UHT.Types;
using UnrealSharpScriptGenerator.Tooltip;
using UnrealSharpScriptGenerator.Utilities;

namespace UnrealSharpScriptGenerator.Exporters;

public static class EnumExporter
{
    public static void ExportEnum(UhtEnum enumObj)
    {
        GeneratorStringBuilder stringBuilder = new GeneratorStringBuilder();
        
        string moduleName = enumObj.GetNamespace();
        
        stringBuilder.GenerateTypeSkeleton(moduleName);
        stringBuilder.AppendTooltip(enumObj);
        
        AttributeBuilder attributeBuilder = AttributeBuilder.CreateAttributeBuilder(enumObj);
        attributeBuilder.AddGeneratedTypeAttribute();
        attributeBuilder.Finish();
        
        stringBuilder.AppendLine(attributeBuilder.ToString());
        
        string underlyingType = UnderlyingTypeToString(enumObj.UnderlyingType);
        stringBuilder.DeclareType("enum", enumObj.GetStructName(), underlyingType, isPartial: false);
        
        stringBuilder.Indent();
        int enumValuesCount = enumObj.EnumValues.Count;
        for (int i = 0; i < enumValuesCount; i++)
        {
            UhtEnumValue enumValue = enumObj.EnumValues[i];

            string toolTip = enumObj.GetMetadata("Tooltip", i);
            stringBuilder.AppendTooltip(toolTip);
            
            string cleanValueName = ScriptGeneratorUtilities.GetCleanEnumValueName(enumObj, enumValue);
            string value = enumValue.Value == -1 ? "," : $" = {enumValue.Value},";
            
            stringBuilder.AppendLine($"{cleanValueName}{value}");
        }
        
        stringBuilder.UnIndent();
        
        stringBuilder.CloseBrace();
        FileExporter.SaveGlueToDisk(enumObj, stringBuilder);
    }
    
    public static string UnderlyingTypeToString(UhtEnumUnderlyingType underlyingType)
    {
        return underlyingType switch
        {
            UhtEnumUnderlyingType.Unspecified => "",
            UhtEnumUnderlyingType.Uint8 => "byte",
            UhtEnumUnderlyingType.Int8 => "sbyte",
            UhtEnumUnderlyingType.Int16 => "short",
            UhtEnumUnderlyingType.Int => "int",
            UhtEnumUnderlyingType.Int32 => "int",
            UhtEnumUnderlyingType.Int64 => "long",
            UhtEnumUnderlyingType.Uint16 => "ushort",
            UhtEnumUnderlyingType.Uint32 => "uint",
            UhtEnumUnderlyingType.Uint64 => "ulong",
            _ => throw new ArgumentOutOfRangeException(nameof(underlyingType), underlyingType, null)
        };
        
    }
}