import { Skeleton } from "@/components/ui/skeleton";

export default function StorageManagementSkeleton() {
    return (
        <div>
            <Skeleton className="w-full h-10 mb-2" />
            <Skeleton className="w-full h-10 mb-2" />
            <Skeleton className="w-full h-10 mb-2" />
        </div>
    )
}