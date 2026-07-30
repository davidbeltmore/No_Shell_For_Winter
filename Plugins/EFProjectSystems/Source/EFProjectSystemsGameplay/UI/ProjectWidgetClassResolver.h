#pragma once

#include "Blueprint/UserWidget.h"
#include "UObject/SoftObjectPath.h"

namespace ProjectWidgetClassResolver
{
	EFPROJECTSYSTEMSGAMEPLAY_API UClass* ResolveWidgetClass(
		const FSoftClassPath& ConfiguredWidgetClass,
		UClass* NativeWidgetClass,
		const TCHAR* ContextName);

	EFPROJECTSYSTEMSGAMEPLAY_API UClass* ResolveWidgetClassWithPriority(
		const FSoftClassPath& ConfiguredWidgetClass,
		UClass* NativeWidgetClass,
		const TCHAR* ContextName,
		FName PriorityGroup);

	template <typename TWidget>
	TSubclassOf<TWidget> ResolveWidgetClass(const FSoftClassPath& ConfiguredWidgetClass, const TCHAR* ContextName)
	{
		UClass* ResolvedClass = ResolveWidgetClass(ConfiguredWidgetClass, TWidget::StaticClass(), ContextName);
		return ResolvedClass ? ResolvedClass : TWidget::StaticClass();
	}

	template <typename TWidget>
	TSubclassOf<TWidget> ResolveWidgetClassWithPriority(const FSoftClassPath& ConfiguredWidgetClass, const TCHAR* ContextName, FName PriorityGroup)
	{
		UClass* ResolvedClass = ResolveWidgetClassWithPriority(ConfiguredWidgetClass, TWidget::StaticClass(), ContextName, PriorityGroup);
		return ResolvedClass ? ResolvedClass : TWidget::StaticClass();
	}

	template <typename TWidget>
	TSubclassOf<TWidget> DiscoverWidgetClass(const TCHAR* ContextName)
	{
		return ResolveWidgetClass<TWidget>(FSoftClassPath(), ContextName);
	}
}
